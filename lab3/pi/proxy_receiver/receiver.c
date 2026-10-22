/*
 * N1 cockpit: wheel state over UDP in, CAN out on canA and canB.
 *
 * Payloads (byte 0 = seq in the low nibble):
 *   CTRL   steer int16 LE (-100..100), throttle, brake (0..100), buttons, CRC-8 of bytes 0-5
 *   HB     own fault (0/1), bus health (bit0 = A ok, bit1 = B ok)
 *   BLINK  state (off/left/right/hazard), phase (1 = lamps on)
 *   FAULT  zone, reason
 *   FORCE  int8
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <net/if.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/can.h>
#include <pigpio.h>

#include "state.h"

/** Configure this **/
#define LOCAL_HOST "172.26.166.54" // IP of local interface
#define R_PORT 8000

#define REMOTE_HOST "172.26.34.95"
#define S_PORT 8001
/** **/

// GPIOs (BCM numbers)
#define UDP_RX_GPIO 2
#define CMD_TX_GPIO 3
#define DEG_A_GPIO 13
#define DEG_B_GPIO 19
#define ERR_GPIO 26
static const int can_tx_gpio[2] = { 5, 6 }; // CAN_TX_A, CAN_TX_B

#define ID_CTRL 0x010
#define ID_FAULT 0x020
#define ID_HB_N1 0x100
#define ID_HB_N2 0x101
#define ID_HB_N3 0x102
#define ID_BLINK 0x200
#define ID_FORCE 0x300

enum { BLINK_OFF, BLINK_LEFT, BLINK_RIGHT, BLINK_HAZARD };
enum { FAULT_HB_LOST = 1, FAULT_TEST_BUTTON = 2 };

#define TICK_MS 20
#define BLINK_TICKS 25    // toggle every 500 ms = 1 Hz
#define TIMEOUT_MS 60     // 3 missed heartbeats or UDP updates
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

static int64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
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

// Every 20 ms: HB_TX, HB_MON, FAULT, BLINK_TX. Only this thread sends these IDs.
static void *tick(void *arg)
{
  uint8_t seq_hb = 0, seq_blink = 0, seq_fault = 0;
  int sent_blink = -1, ticks = 0;
  bool phase = false, was_error = false, was_test = false;
  bool was_failed[2] = { true, true }; // no FAULT for zones that never started
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);

  while (1) {
    next.tv_nsec += TICK_MS * 1000000;
    if (next.tv_nsec >= 1000000000) {
      next.tv_nsec -= 1000000000;
      next.tv_sec++;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

    int64_t now = now_ms();
    // A bus is degraded when we hear no other zone on it
    bool bus_ok[2] = { false, false }, failed[2], other_fault = false;

    pthread_mutex_lock(&lock);
    for (int z = 0; z < 2; z++) {
      bool seen_a = now - last_hb_ms[z][0] <= TIMEOUT_MS;
      bool seen_b = now - last_hb_ms[z][1] <= TIMEOUT_MS;
      bus_ok[0] |= seen_a;
      bus_ok[1] |= seen_b;
      failed[z] = !seen_a && !seen_b;
      other_fault |= zone_fault[z];
    }
    bool test = test_fault;
    bool error = test || other_fault || failed[0] || failed[1] || now - last_udp_ms > TIMEOUT_MS;
    int blink = error ? BLINK_HAZARD : blink_req;
    pthread_mutex_unlock(&lock);

    gpioWrite(DEG_A_GPIO, !bus_ok[0]);
    gpioWrite(DEG_B_GPIO, !bus_ok[1]);
    gpioWrite(ERR_GPIO, error);
    if (error != was_error)
      printf(error ? "ERROR\n" : "OK\n");
    was_error = error;

    uint8_t hb[8] = { seq_hb++ & 0x0F, test, bus_ok[0] | bus_ok[1] << 1 };
    send_both(ID_HB_N1, hb);

    // FAULT once, when a zone is lost or the test button is pressed
    for (int z = 0; z < 2; z++) {
      if (failed[z] && !was_failed[z]) {
        uint8_t f[8] = { seq_fault++ & 0x0F, z + 2, FAULT_HB_LOST };
        send_both(ID_FAULT, f);
      }
      was_failed[z] = failed[z];
    }
    if (test && !was_test) {
      uint8_t f[8] = { seq_fault++ & 0x0F, 1, FAULT_TEST_BUTTON };
      send_both(ID_FAULT, f);
    }
    was_test = test;

    // New state: send now, lamps on. Same state: toggle every 500 ms.
    bool changed = blink != sent_blink;
    if (changed || (blink != BLINK_OFF && ++ticks == BLINK_TICKS)) {
      phase = changed ? blink != BLINK_OFF : !phase;
      sent_blink = blink;
      ticks = 0;
      uint8_t b[8] = { seq_blink++ & 0x0F, blink, phase };
      send_both(ID_BLINK, b);
    }
  }
  return NULL;
}

// Heartbeats from N2/N3, and FORCE forwarded to the proxy
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

    if (f.can_id == ID_HB_N2 || f.can_id == ID_HB_N3) {
      int z = f.can_id - ID_HB_N2;
      pthread_mutex_lock(&lock);
      last_hb_ms[z][bus] = now_ms();
      zone_fault[z] = f.data[1];
      pthread_mutex_unlock(&lock);
    } else if (f.can_id == ID_FORCE) {
      sendto(udp_fd, &f.data[1], 1, 0, (struct sockaddr *)&proxy, sizeof(proxy));
    }
  }
  return NULL;
}

// Enter on stdin: the next CTRL is out of range (bad-input test)
static void *input(void *arg)
{
  int c;
  while ((c = getchar()) != EOF)
    if (c == '\n')
      inject_bad = true;
  return NULL;
}

int main()
{
  if (gpioInitialise() < 0)
    exit(EXIT_FAILURE);
  const int outputs[] = { UDP_RX_GPIO, CMD_TX_GPIO, DEG_A_GPIO, DEG_B_GPIO, ERR_GPIO,
                          can_tx_gpio[0], can_tx_gpio[1] };
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
      bind(can_fd, (struct sockaddr *)&can_addr, sizeof(can_addr)) < 0) {
    perror("can");
    exit(EXIT_FAILURE);
  }

  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in servaddr = { .sin_family = AF_INET, .sin_port = htons(R_PORT) };
  servaddr.sin_addr.s_addr = inet_addr(LOCAL_HOST);
  if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    perror("udp");
    exit(EXIT_FAILURE);
  }

  pthread_t tid;
  pthread_create(&tid, NULL, tick, NULL);
  pthread_create(&tid, NULL, can_rx, NULL);
  pthread_create(&tid, NULL, input, NULL);

  char buf[4 + sizeof(DIJOYSTATE2_t)]; // 4-byte packet counter, then the state
  DIJOYSTATE2_t state;
  uint8_t seq_ctrl = 0;
  bool prev_left = false, prev_right = false, prev_test = false, turned = false;
  int64_t last_test_press = 0;

  while (1) {
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
    uint8_t d[8] = { seq_ctrl++ & 0x0F, s & 0xFF, (uint16_t)s >> 8, throttle, brake,
                     left | right << 1 | test << 2 };
    d[6] = crc8(d, 6);
    send_both(ID_CTRL, d);
    toggle(CMD_TX_GPIO);
  }
}
