#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>


/* Configure the UART pins and start */
int picom_init(void);

/* Send char * over picom link */
void picom_send(const char *s);


#endif

