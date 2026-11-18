/*
 * Cloud mirror publisher. See publisher.h.
 *
 * Stream IDs are the log index ("<index>-0"), so Redis itself refuses an
 * entry it already has. On every (re)connect we read the newest ID in the
 * stream and drop everything up to it from the queue: that is "resume from
 * the last acknowledged stream ID". An entry leaves the queue only once Redis
 * has it.
 *
 * New run: after all nodes restart, the log starts at index 1 again and the
 * old stream would refuse it. A fresh run is the only time the Pi commits
 * index 1 without it being replayed (after a Pi-only reboot, index 1 comes
 * back replayed from the leader), so that entry clears the old stream first.
 * Limit: with LEADER=random, a Pi started well after the STM32s gets index 1
 * replayed too; clear by hand then (on the VM: redis-cli --askpass DEL vehicle:log).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <hiredis/hiredis.h>

#include "catalog.h"
#include "raftlog.h" // entry_name()
#include "publisher.h"

#define QUEUE_SIZE 1000   // handout: keep the last 1,000 entries while the uplink is down
#define HEARTBEAT_MS 1000
#define HEARTBEAT_TTL_S 3 // the GUI shows its stale banner once the key expires
#define POLL_MS 50
// Connect and command timeout, so a dead uplink can't hang us. Generous: campus
// Wi-Fi and Tailscale relays can take over a second, and this thread has no deadline.
#define TIMEOUT_S 5
#define REDIS_PORT 6379

struct pub_entry {
  uint16_t index;
  uint8_t term, entry;
  bool replayed;
  int64_t global_us; // global time when the Pi applied it
};

// Shared with the real-time threads, under q_lock. Never the receiver's `lock`:
// this low-priority thread must not hold anything the real-time threads need
// for long.
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pub_entry queue[QUEUE_SIZE]; // ring buffer, oldest at q_head
static int q_head, q_count;
static int64_t status_global_us;
static int status_leader;
static char status_network[96];
static char status_errors[96];

// Only the publisher thread uses these
static const char *host, *password;
static redisContext *rc; // NULL while disconnected
static bool reported_down;

void publisher_commit(uint16_t index, uint8_t term, uint8_t entry, bool replayed,
                      int64_t global_us)
{
  pthread_mutex_lock(&q_lock);
  if (q_count == QUEUE_SIZE) { // full: the oldest goes
    q_head = (q_head + 1) % QUEUE_SIZE;
    q_count--;
  }
  queue[(q_head + q_count) % QUEUE_SIZE] =
      (struct pub_entry){ index, term, entry, replayed, global_us };
  q_count++;
  pthread_mutex_unlock(&q_lock);
}

void publisher_status(int64_t global_us, int leader, const char *network, const char *errors)
{
  pthread_mutex_lock(&q_lock);
  status_global_us = global_us;
  status_leader = leader;
  snprintf(status_network, sizeof(status_network), "%s", network);
  snprintf(status_errors, sizeof(status_errors), "%s", errors);
  pthread_mutex_unlock(&q_lock);
}

// Redis has everything up to `index`: drop it from the queue
static void drop_through(uint16_t index)
{
  pthread_mutex_lock(&q_lock);
  while (q_count > 0 && queue[q_head].index <= index) {
    q_head = (q_head + 1) % QUEUE_SIZE;
    q_count--;
  }
  pthread_mutex_unlock(&q_lock);
}

// The oldest queued entry starts a new run (see the top of this file)
static bool front_is_new_run(void)
{
  pthread_mutex_lock(&q_lock);
  bool yes = q_count > 0 && queue[q_head].index == 1 && !queue[q_head].replayed;
  pthread_mutex_unlock(&q_lock);
  return yes;
}

static void disconnect(const char *why)
{
  // hiredis reports a reply that took longer than TIMEOUT_S as "Resource
  // temporarily unavailable" (older versions) or "Timed out"
  if (strstr(why, "temporarily unavailable") || strstr(why, "Timed out"))
    why = "no reply within the timeout: slow uplink or busy VM";
  if (!reported_down)
    printf("[N1] PUB %s unreachable (%s), retrying every second\n", host, why);
  reported_down = true;
  if (rc)
    redisFree(rc);
  rc = NULL;
}

// Connect, log in, and skip what the stream already has
static bool connect_redis(void)
{
  struct timeval tv = { .tv_sec = TIMEOUT_S };
  rc = redisConnectWithTimeout(host, REDIS_PORT, tv);
  if (!rc || rc->err) {
    disconnect(rc ? rc->errstr : "out of memory");
    return false;
  }
  redisSetTimeout(rc, tv);

  redisReply *r = redisCommand(rc, "AUTH %s", password);
  bool ok = r && r->type != REDIS_REPLY_ERROR;
  freeReplyObject(r);
  if (!ok) {
    disconnect("login refused, check REDIS_PASSWORD");
    return false;
  }

  r = redisCommand(rc, "XREVRANGE vehicle:log + - COUNT 1");
  if (!r || r->type != REDIS_REPLY_ARRAY) {
    freeReplyObject(r);
    disconnect("can't read vehicle:log");
    return false;
  }
  int last = r->elements ? atoi(r->element[0]->element[0]->str) : 0; // "12-0" -> 12
  freeReplyObject(r);
  if (!front_is_new_run()) // a new run clears the stream instead (send_entries)
    drop_through(last);
  printf("[N1] PUB connected to %s, stream already has up to index %d\n", host, last);
  reported_down = false;
  return true;
}

// Send queued entries, oldest first. False if the connection failed.
static bool send_entries(void)
{
  for (;;) {
    pthread_mutex_lock(&q_lock);
    bool any = q_count > 0;
    struct pub_entry e = queue[q_head];
    pthread_mutex_unlock(&q_lock);
    if (!any)
      return true;

    redisReply *r;
    if (e.index == 1 && !e.replayed) {
      r = redisCommand(rc, "DEL vehicle:log");
      bool ok = r && r->type != REDIS_REPLY_ERROR;
      freeReplyObject(r);
      if (!ok)
        return false;
      printf("[N1] PUB new run: cleared the old stream\n");
    }

    r = redisCommand(
        rc, "XADD vehicle:log %d-0 term %d index %d type %s node %d entry %d replayed %d global_us %lld",
        e.index, e.term, e.index, entry_name(e.entry), ENTRY_NODE(e.entry), e.entry, e.replayed,
        (long long)e.global_us);
    if (!r)
      return false;
    // "equal or smaller than the top item": Redis already has it
    bool done = r->type == REDIS_REPLY_STRING ||
                (r->type == REDIS_REPLY_ERROR && strstr(r->str, "equal or smaller"));
    freeReplyObject(r);
    if (!done)
      return false;
    drop_through(e.index);
  }
}

// The leader is also the time master (team design), so one field covers both.
// TODO (team): the FL/RL offset as the Pi last measured it (needs FL and RL wired to the Pi)
static bool send_heartbeat(void)
{
  struct timespec wall;
  clock_gettime(CLOCK_REALTIME, &wall);
  pthread_mutex_lock(&q_lock);
  long long global_us = status_global_us;
  int leader = status_leader;
  char network[sizeof(status_network)], errors[sizeof(status_errors)];
  memcpy(network, status_network, sizeof(network));
  memcpy(errors, status_errors, sizeof(errors));
  pthread_mutex_unlock(&q_lock);

  redisReply *r = redisCommand(rc, "HSET vehicle:heartbeat wall_ms %lld global_us %lld leader %d network %s errors %s",
                               wall.tv_sec * 1000LL + wall.tv_nsec / 1000000, global_us, leader, network, errors);
  bool ok = r && r->type != REDIS_REPLY_ERROR;
  freeReplyObject(r);
  if (!ok)
    return false;
  r = redisCommand(rc, "EXPIRE vehicle:heartbeat %d", HEARTBEAT_TTL_S);
  ok = r && r->type != REDIS_REPLY_ERROR;
  freeReplyObject(r);
  return ok;
}

static int64_t mono_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void *run(void *arg)
{
  int64_t last_heartbeat = 0;
  for (;;) {
    if (!rc && !connect_redis()) {
      sleep(1);
      continue;
    }
    bool ok = send_entries();
    if (ok && mono_ms() - last_heartbeat >= HEARTBEAT_MS) {
      ok = send_heartbeat();
      last_heartbeat = mono_ms();
    }
    if (!ok) {
      disconnect(rc->err ? rc->errstr : "command refused");
      continue;
    }
    usleep(POLL_MS * 1000);
  }
  return NULL;
}

void publisher_start(void)
{
  password = getenv("REDIS_PASSWORD");
  host = getenv("REDIS_HOST");
  if (!host)
    host = "449-car";
  if (!password) {
    printf("[N1] PUB off: REDIS_PASSWORD not set\n");
    return;
  }
  pthread_t tid;
  pthread_create(&tid, NULL, run, NULL);
  pthread_detach(tid);
}
