#ifndef SERIAL_H
#define SERIAL_H

#include <arch/x86_64/io.h>

#include <klibc/types.h>

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

#define SERIAL_DATA_REG         0
#define SERIAL_INT_ENABLE_REG   1
#define SERIAL_DIVISOR_LOW      0
#define SERIAL_DIVISOR_HIGH     1
#define SERIAL_INT_ID_REG       2
#define SERIAL_FIFO_CTRL_REG    2
#define SERIAL_LINE_CTRL_REG    3
#define SERIAL_MODEM_CTRL_REG   4
#define SERIAL_LINE_STATUS_REG  5
#define SERIAL_MODEM_STATUS_REG 6
#define SERIAL_SCRATCH_REG      7

#define SERIAL_LCR_DLAB         0x80
#define SERIAL_LCR_8N1          0x03

#define SERIAL_LSR_DATA_READY   0x01
#define SERIAL_LSR_THRE         0x20
#define SERIAL_LSR_TEMT         0x40

#define SERIAL_FCR_ENABLE       0x01
#define SERIAL_FCR_CLEAR_RX     0x02
#define SERIAL_FCR_CLEAR_TX     0x04
#define SERIAL_FCR_TRIGGER_14   0xC0

#define SERIAL_MCR_DTR          0x01
#define SERIAL_MCR_RTS          0x02
#define SERIAL_MCR_OUT1         0x04
#define SERIAL_MCR_OUT2         0x08
#define SERIAL_MCR_LOOP         0x10

#define SERIAL_BAUD_115200      1
#define SERIAL_BAUD_57600       2
#define SERIAL_BAUD_38400       3
#define SERIAL_BAUD_19200       6
#define SERIAL_BAUD_9600        12

int  serial_init(uint16_t port);
int  serial_received(uint16_t port);
char serial_read(uint16_t port);
int  serial_is_transmit_empty(uint16_t port);
void serial_write(uint16_t port, char c);
void serial_write_string(uint16_t port, const char *str);

#endif