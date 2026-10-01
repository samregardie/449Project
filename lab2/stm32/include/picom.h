#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>


/* Configure the UART pins and start */
int picom_init(void);

/* Send char * over picom link */
void picom_send(const char *s);

/*
 * Copies the most recent 64 char message from uart buffer to output. 
 * Deletes all messages from uart buffer. Returns if any message was found.
 */
bool picom_read(char *output);

#endif

