#ifndef __MODBUS_UART_H
#define __MODBUS_UART_H
#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>

int modbus_uart_init(void);
int modbus_uart_pass(uint8_t *send_buf, int send_len, uint8_t *read_buf, int read_bufsz, int timeout);

#endif