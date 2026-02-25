#ifndef KB_H
#define KB_H

#include <klibc/types.h>

#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64
#define KB_COMMAND_PORT 0x64

#define KB_STATUS_OUTPUT_FULL   0x01
#define KB_STATUS_INPUT_FULL    0x02
#define KB_STATUS_SYSTEM_FLAG   0x04
#define KB_STATUS_COMMAND       0x08
#define KB_STATUS_TIMEOUT       0x40
#define KB_STATUS_PARITY_ERROR  0x80

#define KB_CMD_SET_LED          0xED
#define KB_CMD_ECHO             0xEE
#define KB_CMD_SET_SCANCODE     0xF0
#define KB_CMD_IDENTIFY         0xF2
#define KB_CMD_SET_RATE         0xF3
#define KB_CMD_ENABLE           0xF4
#define KB_CMD_DISABLE          0xF5
#define KB_CMD_RESET            0xFF

#define KB_RESP_ACK             0xFA
#define KB_RESP_RESEND          0xFE
#define KB_RESP_ERROR           0xFC

#define KB_LED_SCROLL_LOCK      0x01
#define KB_LED_NUM_LOCK         0x02
#define KB_LED_CAPS_LOCK        0x04

#define KB_SC_EXTENDED          0xE0

#define KB_MOD_LSHIFT           (1 << 0)
#define KB_MOD_RSHIFT           (1 << 1)
#define KB_MOD_LCTRL            (1 << 2)
#define KB_MOD_RCTRL            (1 << 3)
#define KB_MOD_LALT             (1 << 4)
#define KB_MOD_RALT             (1 << 5)
#define KB_MOD_CAPS_LOCK        (1 << 6)
#define KB_MOD_NUM_LOCK         (1 << 7)
#define KB_MOD_SCROLL_LOCK      (1 << 8)

#define KB_MOD_SHIFT    (KB_MOD_LSHIFT | KB_MOD_RSHIFT)
#define KB_MOD_CTRL     (KB_MOD_LCTRL | KB_MOD_RCTRL)
#define KB_MOD_ALT      (KB_MOD_LALT | KB_MOD_RALT)

#define KEY_ESC         0x1B
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_SPACE       0x20

#define KEY_F1          0x80
#define KEY_F2          0x81
#define KEY_F3          0x82
#define KEY_F4          0x83
#define KEY_F5          0x84
#define KEY_F6          0x85
#define KEY_F7          0x86
#define KEY_F8          0x87
#define KEY_F9          0x88
#define KEY_F10         0x89
#define KEY_F11         0x8A
#define KEY_F12         0x8B

#define KEY_UP          0x90
#define KEY_DOWN        0x91
#define KEY_LEFT        0x92
#define KEY_RIGHT       0x93
#define KEY_HOME        0x94
#define KEY_END         0x95
#define KEY_PAGE_UP     0x96
#define KEY_PAGE_DOWN   0x97
#define KEY_INSERT      0x98
#define KEY_DELETE      0x99

typedef struct {
    uint8_t  keycode;       /* ASCII or KEY_* special code */
    uint8_t  scancode;      /* Raw scan code (without release bit) */
    uint16_t modifiers;     /* KB_MOD_* flags at time of event */
    bool     pressed;       /* true = key down, false = key up */
} kb_event_t;

typedef void (*kb_callback_t)(kb_event_t *event);

void kb_init(void);

void kb_set_callback(kb_callback_t callback);

void kb_poll(void);

uint16_t kb_get_modifiers(void);

void kb_set_leds(uint8_t leds);
void kb_update_leds_sync(void);

bool    kb_wait_input(void);
bool    kb_wait_output(void);
bool    kb_send_command(uint8_t cmd);
uint8_t kb_read_data(void);

#endif
