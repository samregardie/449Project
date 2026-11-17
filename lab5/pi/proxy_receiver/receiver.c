/*
 * N1 cockpit: wheel state over UDP in, CAN out on canA and canB.
 *
 * The message catalog and log entries are in lab5/stm32-common/include/catalog.h
 * and the Raft config in raft_config.h, both shared with the STM32s (build.sh
 * adds that folder to the include path). N1's HB fault is the test button or a
 * lost wheel link.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <linux/can.h>
#include <pigpio.h>

#include "state.h"
#include "catalog.h"     // CAN IDs, payload layouts, log entries (shared with the STM32s)
#include "raft_config.h" // Raft config (shared with the STM32s)
#include "raftlog.h"     // replicated log and state machine (shared with the STM32s)

/** Configure this **/
//#define LOCAL_HOST "172.26.166.54" // IP of local interface
#define R_PORT 8000

//#define REMOTE_HOST "172.26.34.95"
#define S_PORT 8001

#define LOCAL_HOST "192.168.50.1"
#define REMOTE_HOST "192.168.50.2"

/** **/

// GPIOs (BCM numbers)
#define UDP_RX_GPIO 2
#define CMD_TX_GPIO 3
#define DEG_A_GPIO 13
#define DEG_B_GPIO 19
#define ERR_GPIO 26
#define LEADER_GPIO 16 // high while this node is the Raft leader
#define SYNC_GPIO 20   // high while this node's clock follows global time
#define PPS_GPIO 23    // rising edge on every whole second of global time
#define PPS_HIGH_MS 100
static const int can_tx_gpio[2] = { 5, 6 }; // CAN_TX_A, CAN_TX_B

#define TICK_MS RAFT_HEARTBEAT_MS // REF goes out on every tick
#define BLINK_TICKS 5     // repeat BLINK every 100 ms
#define HB_TIMEOUT_MS 60  // 3 missed heartbeats
#define RELAY_DETECT_MS 30 // one missed heartbeat, plus margin (same as the STM32 relay)
#define UDP_TIMEOUT_MS 200 // wheel link lost. TODO (team): justify; Wi-Fi gaps exceed 60 ms
#define DOUBLE_PRESS_MS 500
#define TURN_THRESHOLD 30 // blinker cancels after steering past this and back

#define SCALE ((32768 + 99) / 100) // int16 -> -100..100, same as Lab 2

static int can_fd;
static int ifindex[2];

// Shared between threads, under `lock`. Zone index 0 is N2, 1 is N3.
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int64_t last_udp_ms;
static int64_t last_hb_ms[2][2]; // [zone][bus]
static uint8_t zone_fault[2];
static bool test_fault;
static int blink_req; // from the paddles

static atomic_bool inject_bad;

// Raft state. Only tick() changes it for now; put it under `lock` once can_rx()
// changes it too (votes, APPEND from another leader).
// With a default leader every node boots in term 1 with that leader (Part 2).
static uint8_t term = RAFT_DEFAULT_LEADER != RAFT_NO_LEADER ? 1 : 0;
static int leader = RAFT_DEFAULT_LEADER; // node ID, RAFT_NO_LEADER = none known
static uint8_t cycle;                    // REF cycle counter; a new leader continues it

// Leader's view of each follower, under `lock` (with the log). Zone 0 is N2, 1 is N3.
static uint16_t next_index[2] = { 1, 1 }; // next entry to send it
static uint16_t match_index[2];           // last entry it confirmed
// ACKs arrive on both buses: act on one copy. (A follower that reboots starts
// its seq at 0 again; if that equals the last one, one ACK is skipped and the
// next APPEND, at most 100 ms later, gets a fresh one.)
static uint8_t last_ack_seq[2] = { 0xFF, 0xFF };
static uint8_t seq_append;
static bool log_full; // rule 7: the log refused an entry, so we stay in error

// Global time = local monotonic clock + offset. The offset is 0 while the Pi
// leads; when it follows another leader (checkoff 7), sync sets the offset.
static int64_t global_offset_us;

// Ctrl+C or kill: threads stop touching GPIO, then pigpio is shut down
static atomic_bool quit;

static void on_signal(int sig)
{
  if (quit)
    _exit(1); // second Ctrl+C: give up on the clean shutdown
  quit = true;
}

static void fail(const char *what)
{
  perror(what);
  gpioTerminate(); // never exit without this: pigpio's DMA would keep running
  exit(EXIT_FAILURE);
}

static int64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

// CLOCK_MONOTONIC, not wall time: NTP can step the wall clock, which would
// make every follower's clock jump.
static int64_t now_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

// Current global time in us (Part 6.3)
static int64_t global_gettime(void)
{
  return now_us() + global_offset_us;
}

static void toggle(int gpio)
{
  gpioWrite(gpio, !gpioRead(gpio));
}

static uint8_t crc8(const uint8_t *data, int len) // polynomial 0x07
{
  uint8_t crc = 0;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
  }
  return crc;
}

// Same frame on A, then B. MSG_DONTWAIT so a bus that is down can't block us.
static void send_both(uint16_t id, const uint8_t data[8])
{
  struct can_frame f = { .can_id = id, .can_dlc = 8 };
  memcpy(f.data, data, 8);

  for (int b = 0; b < 2; b++) {
    struct sockaddr_can addr = { .can_family = AF_CAN, .can_ifindex = ifindex[b] };
    toggle(can_tx_gpio[b]);
    sendto(can_fd, &f, sizeof(f), MSG_DONTWAIT, (struct sockaddr *)&addr, sizeof(addr));
  }
}

// REF (catalog.h): seq, term, cycle, global time in us (5 bytes, little-endian).
// The time is read last, right before sending: anything after it is sync error.
static void send_ref(uint8_t seq)
{
  uint8_t d[8] = { seq & CAN_SEQ_MASK, term, cycle++ };
  uint64_t t = global_gettime();
  for (int i = 0; i < 5; i++)
    d[3 + i] = t >> (8 * i);
  send_both(CAN_ID_REF(NODE_COCKPIT), d);
}

// A follower we can hear: its HB on either bus within HB_TIMEOUT_MS. Under `lock`.
static bool follower_alive(int z, int64_t now)
{
  return now - last_hb_ms[z][0] <= HB_TIMEOUT_MS || now - last_hb_ms[z][1] <= HB_TIMEOUT_MS;
}

// APPEND (catalog.h): seq, term, prev index (2), prev term, entry term, commit distance, entry.
// APPEND is one broadcast frame, so it carries the next entry for the follower
// furthest behind. Followers we can't hear are skipped, or one unplugged node
// would hold everyone back. When everyone is up to date the "next entry" is
// past the end of the log: entry 0, the keep-alive. Under `lock`.
static void send_append_locked(void)
{
  int64_t now = now_ms();
  uint16_t n = log_last_index() + 1;
  int behind = -1; // zone of the follower we send for, -1 = nobody behind
  for (int z = 0; z < 2; z++)
    if (follower_alive(z, now) && next_index[z] < n) {
      n = next_index[z];
      behind = z;
    }

  uint16_t prev = n - 1, commit = log_commit_index();
  int distance = n - commit; // commit <= last < n on a keep-alive, so this is >= 1 there
  if (distance < 0)
    distance = 0;
  if (distance > 255)
    distance = 255; // too low a commit index is safe: the follower catches up later
  uint8_t d[8] = { seq_append++ & CAN_SEQ_MASK, term, prev & 0xFF, prev >> 8, log_term(prev),
                   log_term(n), distance, log_entry(n) };
  send_both(CAN_ID_APPEND(NODE_COCKPIT), d);

  // Debug: print each APPEND that carries an entry once. A resend of the same
  // index for the same follower (every 100 ms while it doesn't ACK) is not
  // printed again, so a stuck follower shows as one line, not a stream.
  static uint16_t printed_n;
  static int printed_for = -2;
  if (RAFT_DEBUG && d[7] != ENTRY_NONE && (n != printed_n || behind != printed_for)) {
    printf("[N1] RAFT APPEND idx=%d term=%d %s (0x%02x) commit=%d for %s\n", n, log_term(n),
           entry_name(d[7]), d[7], commit,
           behind == 0 ? "N2" : behind == 1 ? "N3" : "both");
    printed_n = n;
    printed_for = behind;
  }
}

// The leader adds an entry and sends it now, not on the next 100 ms tick
static void leader_append(uint8_t entry)
{
  pthread_mutex_lock(&lock);
  if (log_append(term, entry))
    send_append_locked();
  else
    log_full = true; // our own entries are always valid, so the log is full
  pthread_mutex_unlock(&lock);
}

// Commit the newest entry of this term that a majority has (the Pi counts
// itself). Older-term entries commit along with it. Under `lock`.
static void advance_commit_locked(void)
{
  for (uint16_t i = log_last_index(); i > log_commit_index(); i--) {
    int count = 1;
    for (int z = 0; z < 2; z++)
      count += match_index[z] >= i;
    if (count >= RAFT_MAJORITY && log_term(i) == term) {
      log_set_commit(i);
      return;
    }
  }
}

// APPEND_ACK (catalog.h): seq, term, success (0/1), match index (2).
// Success: the follower has everything up to `match`. Only ever raise
// match_index on success: every follower ACKs every APPEND, so an up-to-date
// follower ACKs the old entries we resend for a lagging one, with a low
// `match`. Taking that at face value made it look behind too, and each ACK
// triggered another APPEND (a flood).
// Failure: its log didn't fit at prev, and `match` is its last index after
// dropping what conflicted, so we resend from there. A follower that rebooted
// (RAM log) reports 0 and gets the whole log again.
static void on_append_ack(int z, const uint8_t d[8])
{
  uint8_t seq = d[0] & CAN_SEQ_MASK;
  uint16_t match = d[3] | d[4] << 8;

  pthread_mutex_lock(&lock);
  if (seq != last_ack_seq[z]) {
    last_ack_seq[z] = seq;
    if (match > log_last_index())
      match = log_last_index();
    if (d[2]) {
      if (match > match_index[z])
        match_index[z] = match;
      next_index[z] = match_index[z] + 1;
    } else {
      if (match < match_index[z])
        match_index[z] = match; // it lost entries (reboot): stop counting them
      next_index[z] = match + 1;
    }

    advance_commit_locked();
    if (next_index[z] <= log_last_index())
      send_append_locked(); // behind: send its next entry now
    if (RAFT_DEBUG && !d[2])
      printf("[N1] RAFT ACK N%d failed, resend from %d\n", z + 2, next_index[z]);
  }
  pthread_mutex_unlock(&lock);
}

// Apply newly committed entries and print them, the same lines on every node.
// The lock is held only to apply; printing happens outside it.
static void print_commits(void)
{
  bool any = false;
  for (;;) {
    pthread_mutex_lock(&lock);
    uint16_t i = log_apply_next();
    uint8_t t = log_term(i), e = log_entry(i);
    pthread_mutex_unlock(&lock);
    if (i == 0)
      break;
    printf("[N1] COMMIT idx=%d term=%d %s node=%d (0x%02x)\n", i, t, entry_name(e),
           ENTRY_NODE(e), e);
    any = true;
  }

  if (any) {
    char buf[96];
    pthread_mutex_lock(&lock);
    struct network net = get_network();
    pthread_mutex_unlock(&lock);
    network_format(&net, buf, sizeof(buf));
    printf("[N1] NET %s\n", buf);
  }
}

// Every 20 ms: REF and APPEND (while leader), HB_TX, HB_MON, FAULT, BLINK_TX.
// Only this thread sends these IDs.
static void *tick(void *arg)
{
  uint8_t seq_hb = 0, seq_blink = 0, seq_fault = 0, seq_ref = 0;
  int sent_blink = -1, ticks = 0, append_ticks = 0;
  bool was_error = false, was_test = false;
  bool was_failed[2] = { true, true }; // no FAULT for zones that never started
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);

  while (!quit) {
    next.tv_nsec += TICK_MS * 1000000;
    if (next.tv_nsec >= 1000000000) {
      next.tv_nsec -= 1000000000;
      next.tv_sec++;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

    // Raft first, so the rest of the tick adds no jitter to REF. Sent in the
    // error state too: leadership and sync don't depend on failure mode.
    bool is_leader = leader == NODE_COCKPIT;
    if (is_leader) {
      send_ref(seq_ref++);
      if (++append_ticks == RAFT_APPEND_MS / RAFT_HEARTBEAT_MS) {
        append_ticks = 0;
        pthread_mutex_lock(&lock);
        send_append_locked();
        pthread_mutex_unlock(&lock);
      }
    }
    gpioWrite(LEADER_GPIO, is_leader);
    gpioWrite(SYNC_GPIO, is_leader); // the leader's clock is global time by definition

    int64_t now = now_ms();
    // A bus is degraded when we hear no other zone on it
    bool bus_ok[2] = { false, false }, failed[2], other_fault = false;

    pthread_mutex_lock(&lock);
    for (int z = 0; z < 2; z++) {
      bool seen_a = now - last_hb_ms[z][0] <= HB_TIMEOUT_MS;
      bool seen_b = now - last_hb_ms[z][1] <= HB_TIMEOUT_MS;
      bus_ok[0] |= seen_a;
      bus_ok[1] |= seen_b;
      failed[z] = !seen_a && !seen_b;
      other_fault |= zone_fault[z];
    }
    bool test = test_fault;
    int64_t udp_age = now - last_udp_ms;
    bool own_fault = test || udp_age > UDP_TIMEOUT_MS || log_full; // in HB_N1, so N2/N3 follow
    bool error = own_fault || other_fault || failed[0] || failed[1];
    int blink = error ? BLINK_HAZARD : blink_req;
    pthread_mutex_unlock(&lock);

    gpioWrite(DEG_A_GPIO, !bus_ok[0]);
    gpioWrite(DEG_B_GPIO, !bus_ok[1]);
    gpioWrite(ERR_GPIO, error);
    if (error != was_error)
      printf("%s: test %d, zone fault %d, N2 lost %d, N3 lost %d, last UDP %lld ms ago\n",
             error ? "ERROR" : "OK", test, other_fault, failed[0], failed[1], (long long)udp_age);
    was_error = error;

    uint8_t hb[8] = { seq_hb++ & CAN_SEQ_MASK, own_fault, bus_ok[0] | bus_ok[1] << 1 };
    send_both(CAN_ID_HB_N1, hb);

    // FAULT once, when a zone is lost or the test button is pressed
    for (int z = 0; z < 2; z++) {
      if (failed[z] && !was_failed[z]) {
        uint8_t f[8] = { seq_fault++ & CAN_SEQ_MASK, z + 2, FAULT_HB_LOST };
        send_both(CAN_ID_FAULT, f);
      }
      was_failed[z] = failed[z];
    }
    if (test && !was_test) {
      uint8_t f[8] = { seq_fault++ & CAN_SEQ_MASK, 1, FAULT_TEST_BUTTON };
      send_both(CAN_ID_FAULT, f);
    }
    was_test = test;

    // State only: the STM32s time the lamps from global time.
    // Send on change, and every 100 ms so a node that missed one catches up.
    bool changed = blink != sent_blink;
    if (changed || ++ticks == BLINK_TICKS) {
      sent_blink = blink;
      ticks = 0;
      uint8_t b[8] = { seq_blink++ & CAN_SEQ_MASK, blink };
      send_both(CAN_ID_BLINK, b);
    }

    // Last, after every frame of this tick is out
    print_commits();
  }
  return NULL;
}

// N2 and N3 have no bus in common: one is silent on A, the other on B. Under `lock`.
static bool split(int64_t now)
{
  bool silent[2][2];
  for (int z = 0; z < 2; z++)
    for (int b = 0; b < 2; b++)
      silent[z][b] = now - last_hb_ms[z][b] > RELAY_DETECT_MS;
  return (silent[0][0] && silent[1][1]) || (silent[0][1] && silent[1][0]);
}

// Heartbeats from N2/N3, and FORCE forwarded to the proxy.
// RELAY: while N2 and N3 share no bus, their HBs and FAULTs go to the other bus
// unchanged (same ID and seq, so receivers dedup). Everything else N1 sends on both.
static void *can_rx(void *arg)
{
  int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in proxy = { .sin_family = AF_INET, .sin_port = htons(S_PORT) };
  proxy.sin_addr.s_addr = inet_addr(REMOTE_HOST);

  while (1) {
    struct can_frame f;
    struct sockaddr_can addr;
    socklen_t len = sizeof(addr);

    if (recvfrom(can_fd, &f, sizeof(f), 0, (struct sockaddr *)&addr, &len) != sizeof(f))
      continue;
    int bus = addr.can_ifindex == ifindex[0] ? 0 : 1;

    if (f.can_id == CAN_ID_HB_N2 || f.can_id == CAN_ID_HB_N3) {
      int z = f.can_id - CAN_ID_HB_N2;
      pthread_mutex_lock(&lock);
      last_hb_ms[z][bus] = now_ms();
      zone_fault[z] = f.data[1];
      pthread_mutex_unlock(&lock);
    } else if (f.can_id == CAN_ID_FORCE) {
      sendto(udp_fd, &f.data[1], 1, 0, (struct sockaddr *)&proxy, sizeof(proxy));
    } else if (f.can_id == CAN_ID_APPEND_ACK(NODE_STEERING)) {
      on_append_ack(0, f.data);
    } else if (f.can_id == CAN_ID_APPEND_ACK(NODE_DRIVE)) {
      on_append_ack(1, f.data);
    }

    if (f.can_id == CAN_ID_HB_N2 || f.can_id == CAN_ID_HB_N3 || f.can_id == CAN_ID_FAULT) {
      pthread_mutex_lock(&lock);
      bool relay = split(now_ms());
      pthread_mutex_unlock(&lock);
      if (relay) {
        struct sockaddr_can to = { .can_family = AF_CAN, .can_ifindex = ifindex[!bus] };
        sendto(can_fd, &f, sizeof(f), MSG_DONTWAIT, (struct sockaddr *)&to, sizeof(to));
      }
    }
  }
  return NULL;
}

// Test keys on stdin:
//   Enter       the next CTRL is out of range (bad-input test)
//   e + Enter   the leader appends an EVENT for N1 (log replication test)
// read(), not getchar(): getchar() locks stdin while it waits, and
// gpioTerminate()'s fflush(NULL) would then wait forever for that lock
static void *input(void *arg)
{
  char c, prev = '\n';
  while (read(STDIN_FILENO, &c, 1) == 1) {
    if (c == '\n') {
      if (prev == 'e')
        leader_append(ENTRY(ENTRY_EVENT, NODE_COCKPIT));
      else
        inject_bad = true;
    }
    prev = c;
  }
  return NULL;
}

// PPS (Part 6.4): rising edge on every whole second of global time, then the
// same print as the STM32s. While the Pi leads, its clock is global time, so
// ratio 1 and no correction; that changes once it can follow another leader.
static void *pps(void *arg)
{
  while (!quit) {
    int64_t next = (global_gettime() / 1000000 + 1) * 1000000;
    int64_t at = next - global_offset_us; // the same instant on CLOCK_MONOTONIC
    struct timespec ts = { .tv_sec = at / 1000000, .tv_nsec = at % 1000000 * 1000 };
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);

    gpioWrite(PPS_GPIO, 1);
    int64_t g = global_gettime(), l = now_us();
    printf("[N1] PPS global=%lld.%06lld local=%lld.%06lld ratio=1.000000 corr=0us step=0us\n",
           (long long)(g / 1000000), (long long)(g % 1000000),
           (long long)(l / 1000000), (long long)(l % 1000000));
    usleep(PPS_HIGH_MS * 1000);
    gpioWrite(PPS_GPIO, 0);
  }
  return NULL;
}

int main()
{
  if (gpioInitialise() < 0)
    exit(EXIT_FAILURE);
  gpioSetSignalFunc(SIGINT, on_signal);
  gpioSetSignalFunc(SIGTERM, on_signal);
  const int outputs[] = { UDP_RX_GPIO, CMD_TX_GPIO, DEG_A_GPIO, DEG_B_GPIO, ERR_GPIO,
                          LEADER_GPIO, SYNC_GPIO, PPS_GPIO, can_tx_gpio[0], can_tx_gpio[1] };
  for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); i++) {
    gpioSetMode(outputs[i], PI_OUTPUT);
    gpioWrite(outputs[i], PI_LOW);
  }

  // ifindex 0 receives from both interfaces; recvfrom says which one
  struct sockaddr_can can_addr = { .can_family = AF_CAN, .can_ifindex = 0 };
  can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  ifindex[0] = if_nametoindex("canA");
  ifindex[1] = if_nametoindex("canB");
  if (!ifindex[0] || !ifindex[1] ||
      bind(can_fd, (struct sockaddr *)&can_addr, sizeof(can_addr)) < 0)
    fail("can");

  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in servaddr = { .sin_family = AF_INET, .sin_port = htons(R_PORT) };
  servaddr.sin_addr.s_addr = inet_addr(LOCAL_HOST);
  if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    fail("udp");

  // recv() gives up after 100 ms, so the loop sees `quit` even with no UDP
  struct timeval timeout = { .tv_usec = 100000 };
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  pthread_t tick_tid, pps_tid, tid;
  // SCHED_FIFO so Linux load can't delay HB_TX/HB_MON long enough to look like
  // a lost zone. Threads created after this inherit it. Below the kernel's IRQ
  // threads (50), which deliver our CAN frames.
  struct sched_param sp = { .sched_priority = 40 };
  if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
    perror("SCHED_FIFO");

  // One line with every Raft setting: at checkoff, proof all three nodes match.
  // The STM32s print the same line (errorstate.c); keep the two in step.
  printf("N1 config: N=%d default_leader=%d seed=%d heartbeat_ms=%d append_ms=%d "
         "missed_heartbeats=%d jitter_ms=%d log_size=%d debug=%d\n",
         RAFT_N, RAFT_DEFAULT_LEADER, RAFT_SEED, RAFT_HEARTBEAT_MS, RAFT_APPEND_MS,
         RAFT_MISSED_HEARTBEATS, RAFT_ELECTION_JITTER_MS, RAFT_LOG_SIZE, RAFT_DEBUG);
  printf("N1 boot: term %d, leader %d\n", term, leader);
  raftlog_init(); // before the threads: they all use the log

  pthread_create(&tick_tid, NULL, tick, NULL);
  pthread_create(&pps_tid, NULL, pps, NULL);
  pthread_create(&tid, NULL, can_rx, NULL);
  pthread_create(&tid, NULL, input, NULL);

  char buf[4 + sizeof(DIJOYSTATE2_t)]; // 4-byte packet counter, then the state
  DIJOYSTATE2_t state;
  uint8_t seq_ctrl = 0;
  bool prev_left = false, prev_right = false, prev_test = false, turned = false;
  int64_t last_test_press = 0;

  while (!quit) {
    if (recv(sockfd, buf, sizeof(buf), 0) <= 4)
      continue;
    toggle(UDP_RX_GPIO);
    memcpy(&state, buf + 4, sizeof(state));

    int8_t steer = state.lX / SCALE;
    int8_t throttle = state.lY / SCALE;
    int8_t brake = state.lRz / SCALE;
    bool left = state.rgbButtons[5], right = state.rgbButtons[4], test = state.rgbButtons[1];
    int64_t now = now_ms();

    pthread_mutex_lock(&lock);
    last_udp_ms = now;

    // Single press enters error, a second press within 500 ms exits
    if (test && !prev_test) {
      bool double_press = test_fault && now - last_test_press <= DOUBLE_PRESS_MS;
      test_fault = !double_press;
      last_test_press = now;
    }

    // A paddle starts that side's blinker, or cancels one that is on
    if ((left && !prev_left) || (right && !prev_right)) {
      blink_req = blink_req != BLINK_OFF ? BLINK_OFF : left ? BLINK_LEFT : BLINK_RIGHT;
      turned = false;
    }

    // Self-cancel once the wheel has turned that way and come back
    if ((blink_req == BLINK_LEFT && steer < -TURN_THRESHOLD) ||
        (blink_req == BLINK_RIGHT && steer > TURN_THRESHOLD))
      turned = true;
    if (turned && abs(steer) < TURN_THRESHOLD)
      blink_req = BLINK_OFF;
    pthread_mutex_unlock(&lock);

    prev_left = left;
    prev_right = right;
    prev_test = test;

    int16_t s = atomic_exchange(&inject_bad, false) ? 1000 : steer;
    uint8_t d[8] = { seq_ctrl++ & CAN_SEQ_MASK, s & 0xFF, (uint16_t)s >> 8, throttle, brake,
                     left | right << 1 | test << 2 };
    d[6] = crc8(d, 6);
    send_both(CAN_ID_CTRL, d);
    toggle(CMD_TX_GPIO);
  }

  // can_rx and input never touch GPIO, so only tick and pps have to stop first
  // (pps can take up to ~1 s: it may be waiting for the next second)
  pthread_join(tick_tid, NULL);
  pthread_join(pps_tid, NULL);
  gpioTerminate();
  return 0;
}
