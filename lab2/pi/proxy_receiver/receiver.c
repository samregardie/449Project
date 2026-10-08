#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdbool.h>
#include <pigpio.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "pthread.h"
#include <inttypes.h>

#include "state.h"

/** Configure this **/
#define LOCAL_HOST "172.26.166.54" // IP of local interface
#define R_PORT 8000

#define REMOTE_HOST "172.26.60.74"
#define S_PORT 8001
/** **/

#define UDP_RX_GPIO 2
#define CMD_TX_GPIO 3

#define TX_INTERVAL_MS 300
#define STATE_SIZE sizeof(DIJOYSTATE2_t)

#define UART_BAUD B115200
#define MAG_INT16_MIN 32768

#define STATUS_FRAME_LEN 5

void *send_force(void *arg)
{
  int8_t force = 0;
  int uart_fd = *(int *)arg;
  int sockfd;
  struct sockaddr_in servaddr;

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(S_PORT);
  servaddr.sin_addr.s_addr = inet_addr(REMOTE_HOST);

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    perror("failed to create socket");
    exit(EXIT_FAILURE);
  }

  int8_t frame[16];
  size_t len = 0;

  while (1)
  {
    //printf("receiv loop len:%d\n", len);
    int8_t byte;
    ssize_t n = read(uart_fd, &byte, 1);

    //printf("%d\n", byte);

    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      perror("uart read");
      break;
    } else if (n == 0) continue;
    
    if (len == sizeof(frame)) {
      len = 0; // no terminator found; drop and resync
      //printf("no frame found \n");
    }
    frame[len] = byte;
    len++;

    if (byte == 0x7F) {
      if (len == STATUS_FRAME_LEN) {
        int8_t force = (int8_t)frame[2];
        printf("Motor Left: %d, Motor Right: %d, Servo: %d, Status: %08b\n", frame[0], frame[1], force, frame[3]);
        //sendto(sockfd, &force, 1, 0,
        //       (struct sockaddr *)&servaddr, sizeof(servaddr));
      }
      len = 0;
    }
  }
}

int uart_open(const char *dev, speed_t baud)
{
  int fd = open(dev, O_RDWR | O_NOCTTY);
  if (fd < 0)
  {
    perror("open");
    return -1;
  }

  struct termios tty;
  if (tcgetattr(fd, &tty) != 0)
  {
    perror("tcgetattr");
    close(fd);
    return -1;
  }

  cfmakeraw(&tty); // raw mode: no translation of bytes
  cfsetispeed(&tty, baud);
  cfsetospeed(&tty, baud);

  tty.c_cflag |= CLOCAL | CREAD; // ignore modem lines, enable receiver
  tty.c_cflag &= ~CSTOPB;        // 1 stop bit
  tty.c_cflag &= ~PARENB;        // no parity
  tty.c_cflag &= ~CRTSCTS;       // no hardware flow control

  tty.c_cc[VMIN] = 0;   // read() returns immediately...
  tty.c_cc[VTIME] = 10; // ...or after 1 s timeout (tenths of a second)

  if (tcsetattr(fd, TCSANOW, &tty) != 0)
  {
    perror("tcsetattr");
    close(fd);
    return -1;
  }
  return fd;
}

int main()
{
  int sockfd;
  char buffer[STATE_SIZE + 1];

  struct sockaddr_in servaddr = {0};

  // Initialize GPIO
  gpioInitialise();
  gpioSetMode(UDP_RX_GPIO, PI_OUTPUT); // Set GPIO2 as input.
  gpioSetMode(CMD_TX_GPIO, PI_OUTPUT); // Set GPIO3 as input.

  gpioWrite(UDP_RX_GPIO, PI_LOW);
  gpioWrite(CMD_TX_GPIO, PI_LOW);

  int uart_fd = uart_open("/dev/ttyAMA5", UART_BAUD);

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    perror("failed to create socket");
    exit(EXIT_FAILURE);
  }

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(R_PORT);
  servaddr.sin_addr.s_addr = inet_addr(LOCAL_HOST);

  if (bind(sockfd, (const struct sockaddr *)&servaddr,
           sizeof(servaddr)) < 0)
  {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  DIJOYSTATE2_t state;
  char recvbuf[sizeof(DIJOYSTATE2_t) + 4];
  pthread_t send_tid;
  pthread_create(&send_tid, NULL, send_force, &uart_fd);

  write(uart_fd, "Test from Pi\n\x7F", 14);

  while (1)
  {
    // printf("Wait recv\n");
    int n, len;
    n = recvfrom(sockfd, recvbuf, STATE_SIZE, MSG_WAITALL,
                 (struct sockaddr *)&servaddr, &len);
    gpioWrite(UDP_RX_GPIO, !gpioRead(UDP_RX_GPIO)); // Toggle the gpio when read complete
    uint32_t packet_ct = ((uint32_t *)recvbuf)[0];
    memcpy(&state, recvbuf + 4, sizeof(state));
    // printf("Receive state (Pkt: %8X) :  Wheel: %d | Throttle: %d | Brake: %d\n", packet_ct, state.lX, state.lY, state.lRz);

    // Data scaling for stm32
    int8_t steer = state.lX / ((MAG_INT16_MIN + 99) / 100);    // Denominator is division by 100 but always round up
    int8_t throttle = state.lY / ((MAG_INT16_MIN + 99) / 100); // Denominator is division by 100 but always round up
    int8_t brake = state.lRz / ((MAG_INT16_MIN + 99) / 100);   // Denominator is division by 100 but always round up
    bool left_signal = state.rgbButtons[5];
    bool right_signal = state.rgbButtons[4];
    bool error_button = state.rgbButtons[1];

    // throttle = (2*throttle) - 100;	// shift from 0 to 100 to -100 to 100
    // brake = (2*brake) - 100;		// shift from 0 to 100 to -100 to 100

    //printf("steer=%d, throttle=%d, brake=%d, left=%d, right=%d, error=%d \n", steer, throttle, brake, left_signal, right_signal, error_button);

    // Send data over uart
    uint8_t buf[] = {steer, throttle, brake, left_signal, right_signal, error_button, '\n', 0x7F};
    write(uart_fd, buf, sizeof(buf));
    gpioWrite(CMD_TX_GPIO, !gpioRead(CMD_TX_GPIO)); // Toggle the gpio on write
    //printf("%s\n", buf);
  }

  return 0;
}
