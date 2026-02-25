#include <drivers/input/kb.h>

#include <arch/x86_64/ioapic.h>
#include <arch/x86_64/apic.h>
#include <arch/x86_64/intr.h>
#include <arch/x86_64/io.h>

#include <video/printk.h>

#include <stddef.h>
#include <stdbool.h>

#define KB_IRQ          1
#define KB_IRQ_VECTOR   33      /* vector = 32 + IRQ number */

#define KB_BUF_SIZE     256     /* must be a power of 2 */
#define KB_BUF_MASK     (KB_BUF_SIZE - 1)

static volatile uint8_t  kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_buf_head = 0;   /* written by IRQ handler */
static volatile uint32_t kb_buf_tail = 0;   /* read  by kb_poll()     */

static const uint8_t sc_normal[128] = {
    0,      KEY_ESC,'1',  '2',  '3',  '4',  '5',  '6',   /* 00-07 */
    '7',    '8',   '9',  '0',  '-',  '=',  KEY_BACKSPACE, KEY_TAB, /* 08-0F */
    'q',    'w',   'e',  'r',  't',  'y',  'u',  'i',   /* 10-17 */
    'o',    'p',   '[',  ']',  KEY_ENTER, 0, 'a', 's',   /* 18-1F */
    'd',    'f',   'g',  'h',  'j',  'k',  'l',  ';',   /* 20-27 */
    '\'',   '`',   0,    '\\', 'z',  'x',  'c',  'v',   /* 28-2F */
    'b',    'n',   'm',  ',',  '.',  '/',  0,    '*',   /* 30-37 */
    0,      ' ',   0,    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, /* 38-3F */
    KEY_F6, KEY_F7,KEY_F8,KEY_F9,KEY_F10,0,  0,   '7',  /* 40-47 */
    '8',    '9',   '-',  '4',  '5',  '6',  '+',  '1',  /* 48-4F */
    '2',    '3',   '0',  '.',  0,    0,    0,    KEY_F11,/* 50-57 */
    KEY_F12,0,     0,    0,    0,    0,    0,    0,     /* 58-5F */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,                  /* 60-6F */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0                   /* 70-7F */
};

static const uint8_t sc_shift[128] = {
    0,      KEY_ESC,'!',  '@',  '#',  '$',  '%',  '^',   /* 00-07 */
    '&',    '*',   '(',  ')',  '_',  '+',  KEY_BACKSPACE, KEY_TAB, /* 08-0F */
    'Q',    'W',   'E',  'R',  'T',  'Y',  'U',  'I',   /* 10-17 */
    'O',    'P',   '{',  '}',  KEY_ENTER, 0, 'A', 'S',   /* 18-1F */
    'D',    'F',   'G',  'H',  'J',  'K',  'L',  ':',   /* 20-27 */
    '"',    '~',   0,    '|',  'Z',  'X',  'C',  'V',   /* 28-2F */
    'B',    'N',   'M',  '<',  '>',  '?',  0,    '*',   /* 30-37 */
    0,      ' ',   0,    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, /* 38-3F */
    KEY_F6, KEY_F7,KEY_F8,KEY_F9,KEY_F10,0,  0,   '7',  /* 40-47 */
    '8',    '9',   '-',  '4',  '5',  '6',  '+',  '1',  /* 48-4F */
    '2',    '3',   '0',  '.',  0,    0,    0,    KEY_F11,/* 50-57 */
    KEY_F12,0,     0,    0,    0,    0,    0,    0,     /* 58-5F */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,                  /* 60-6F */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0                   /* 70-7F */
};

static const struct { uint8_t sc; uint8_t key; } sc_extended[] = {
    { 0x48, KEY_UP    },
    { 0x50, KEY_DOWN  },
    { 0x4B, KEY_LEFT  },
    { 0x4D, KEY_RIGHT },
    { 0x47, KEY_HOME  },
    { 0x4F, KEY_END   },
    { 0x49, KEY_PAGE_UP   },
    { 0x51, KEY_PAGE_DOWN },
    { 0x52, KEY_INSERT },
    { 0x53, KEY_DELETE },
    { 0,    0 }         /* sentinel */
};

static uint16_t      kb_modifiers = 0;
static uint8_t       kb_led_state = 0;
static kb_callback_t kb_callback  = NULL;

static void kb_irq_handler(struct interrupt_frame *frame) {
    (void)frame;

    if (!(inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL))
        return;

    uint8_t sc = inb(KB_DATA_PORT);

    uint32_t next = (kb_buf_head + 1) & KB_BUF_MASK;
    if (next != kb_buf_tail) {
        kb_buf[kb_buf_head] = sc;
        kb_buf_head = next;
    }
}

void kb_poll(void) {
    static bool extended = false;

    while (kb_buf_tail != kb_buf_head) {
        uint8_t sc = kb_buf[kb_buf_tail];
        kb_buf_tail = (kb_buf_tail + 1) & KB_BUF_MASK;

        if (sc == KB_SC_EXTENDED) {
            extended = true;
            continue;
        }

        bool ext      = extended;
        extended      = false;

        bool released = (sc & 0x80) != 0;
        sc &= 0x7F;

        bool mod_changed = false;

        if (!ext) {
            switch (sc) {
            case 0x2A: /* L-Shift */
                if (released) kb_modifiers &= ~KB_MOD_LSHIFT;
                else           kb_modifiers |=  KB_MOD_LSHIFT;
                mod_changed = true; break;
            case 0x36: /* R-Shift */
                if (released) kb_modifiers &= ~KB_MOD_RSHIFT;
                else           kb_modifiers |=  KB_MOD_RSHIFT;
                mod_changed = true; break;
            case 0x1D: /* L-Ctrl */
                if (released) kb_modifiers &= ~KB_MOD_LCTRL;
                else           kb_modifiers |=  KB_MOD_LCTRL;
                mod_changed = true; break;
            case 0x38: /* L-Alt */
                if (released) kb_modifiers &= ~KB_MOD_LALT;
                else           kb_modifiers |=  KB_MOD_LALT;
                mod_changed = true; break;
            case 0x3A: /* Caps Lock (toggle on press only) */
                if (!released) {
                    kb_modifiers ^= KB_MOD_CAPS_LOCK;
                    kb_update_leds_sync();
                }
                mod_changed = true; break;
            case 0x45: /* Num Lock */
                if (!released) {
                    kb_modifiers ^= KB_MOD_NUM_LOCK;
                    kb_update_leds_sync();
                }
                mod_changed = true; break;
            case 0x46: /* Scroll Lock */
                if (!released) {
                    kb_modifiers ^= KB_MOD_SCROLL_LOCK;
                    kb_update_leds_sync();
                }
                mod_changed = true; break;
            }
        } else {
            switch (sc) {
            case 0x1D: /* R-Ctrl */
                if (released) kb_modifiers &= ~KB_MOD_RCTRL;
                else           kb_modifiers |=  KB_MOD_RCTRL;
                mod_changed = true; break;
            case 0x38: /* R-Alt */
                if (released) kb_modifiers &= ~KB_MOD_RALT;
                else           kb_modifiers |=  KB_MOD_RALT;
                mod_changed = true; break;
            }
        }

        uint8_t keycode = 0;

        if (ext) {
            for (int i = 0; sc_extended[i].sc != 0; i++) {
                if (sc_extended[i].sc == sc) {
                    keycode = sc_extended[i].key;
                    break;
                }
            }
        } else if (sc < 128) {
            bool shifted = (kb_modifiers & KB_MOD_SHIFT) != 0;
            bool caps    = (kb_modifiers & KB_MOD_CAPS_LOCK) != 0;

            if (shifted) {
                keycode = sc_shift[sc];
            } else {
                keycode = sc_normal[sc];
                if (caps && keycode >= 'a' && keycode <= 'z')
                    keycode = keycode - 'a' + 'A';
            }
        }

        if (kb_callback && (keycode != 0 || mod_changed)) {
            kb_event_t ev = {
                .keycode   = keycode,
                .scancode  = sc,
                .modifiers = kb_modifiers,
                .pressed   = !released
            };
            kb_callback(&ev);
        }
    }
}

uint16_t kb_get_modifiers(void) {
    return kb_modifiers;
}

void kb_set_callback(kb_callback_t cb) {
    kb_callback = cb;
}

bool kb_wait_input(void) {
    uint32_t timeout = 100000;
    while (timeout--)
        if (!(inb(KB_STATUS_PORT) & KB_STATUS_INPUT_FULL))
            return true;
    return false;
}

bool kb_wait_output(void) {
    uint32_t timeout = 100000;
    while (timeout--)
        if (inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL)
            return true;
    return false;
}

bool kb_send_command(uint8_t cmd) {
    if (!kb_wait_input()) return false;
    outb(KB_DATA_PORT, cmd);
    if (!kb_wait_output()) return false;
    return inb(KB_DATA_PORT) == KB_RESP_ACK;
}

uint8_t kb_read_data(void) {
    return inb(KB_DATA_PORT);
}

void kb_set_leds(uint8_t leds) {
    kb_led_state = leds & 0x07;
    if (kb_send_command(KB_CMD_SET_LED))
        kb_send_command(kb_led_state);
}

void kb_update_leds_sync(void) {
    uint8_t leds = 0;
    if (kb_modifiers & KB_MOD_SCROLL_LOCK) leds |= KB_LED_SCROLL_LOCK;
    if (kb_modifiers & KB_MOD_NUM_LOCK)    leds |= KB_LED_NUM_LOCK;
    if (kb_modifiers & KB_MOD_CAPS_LOCK)   leds |= KB_LED_CAPS_LOCK;
    if (leds != kb_led_state)
        kb_set_leds(leds);
}

void kb_init(void) {
    printk_ts("kb: initializing PS/2 keyboard driver\n");

    int n = 0;
    while ((inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL) && n++ < 16) {
        inb(KB_DATA_PORT);
        io_wait();
    }

    irq_install_handler(KB_IRQ, kb_irq_handler);

    uint8_t apic_id = (uint8_t)apic_get_id();
    int result = ioapic_map_isa_irq(KB_IRQ, KB_IRQ_VECTOR, apic_id);
    if (result != 0) {
        printk("kb: failed to configure IOAPIC for IRQ1\n");
        return;
    }

    uint32_t gsi = ioapic_get_gsi_for_isa_irq(KB_IRQ);
    ioapic_mask_irq(gsi);

    if (!kb_send_command(KB_CMD_ENABLE))
        printk("kb: warning: keyboard did not ACK enable\n");

    kb_set_leds(0);

    n = 0;
    while ((inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL) && n++ < 16) {
        inb(KB_DATA_PORT);
        io_wait();
    }

    ioapic_unmask_irq(gsi);

    printk("kb: PS/2 keyboard ready (IRQ%d -> vector %d)\n",
           KB_IRQ, KB_IRQ_VECTOR);
}
