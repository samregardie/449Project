#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>


/* Configure the UART pins and start */
int picom_init(void);

/* Send char * terminated by 0x7F byte over picom link */
void picom_send(const char *s);

/*
 * Blocks until the next message arrives and copies it (64 chars, ending in
 * 0x7F) to output. For the parser thread; don't mix with picom_read().
 */
void picom_wait(char *output);

/*
 * Copies the most recent 64 char message from uart buffer to output. 
 * Deletes all messages from uart buffer. Returns if any message was found.
 */
bool picom_read(char *output);

#endif

