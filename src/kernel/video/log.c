#include <video/log.h>

static int _indent = 0;
static int _ebegin_col = -1;

static void _pad_to_badge(const char *badge, const char *color)
{
    /* Badge is always "[ XX ]" = 6 visible chars */
    int badge_w  = 6;
    int target   = EINFO_COLS - badge_w;
    int spaces   = target - _ebegin_col;

    if (spaces < 1)
        spaces = 1;

    while (spaces--)
        printk(" ");

    printk("%s[ %s ]%s\n", color, badge, _C_RESET);

    _ebegin_col = -1;
}

static void _prefix(const char *star_color)
{
    int i;
    if (_ebegin_col >= 0) {
        printk("\n");
        _ebegin_col = -1;
    }
    printk(" %s*%s ", star_color, _C_RESET);
    for (i = 0; i < _indent; i++)
        printk(" ");
}

void einfo(const char *fmt, ...)
{
    va_list args;
    _prefix(_C_BGREEN);
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    printk("\n");
}

void ewarn(const char *fmt, ...)
{
    va_list args;
    _prefix(_C_BYELLOW);
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    printk("\n");
}

void eerror(const char *fmt, ...)
{
    va_list args;
    _prefix(_C_BRED);
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    printk("\n");
}

void ebegin(const char *fmt, ...)
{
    va_list args;
    char buf[256];
    int msglen;

    va_start(args, fmt);
    msglen = vsnprintk(buf, sizeof(buf), fmt, args);
    va_end(args);

    _prefix(_C_BGREEN);
    printk("%s ...", buf);

    _ebegin_col = 3 + _indent + msglen + 4;
}

int eend(int retval, const char *errmsg)
{
    int was_interrupted = (_ebegin_col < 0);

    if (!was_interrupted) {
        if (retval == 0)
            _pad_to_badge("ok", _C_BGREEN);
        else {
            _pad_to_badge("!!", _C_BRED);
            if (errmsg) {
                eindent();
                eerror("ERROR: %s", errmsg);
                eoutdent();
            }
        }
    } else {
        _ebegin_col = -1;
        if (retval != 0 && errmsg) {
            eindent();
            eerror("ERROR: %s", errmsg);
            eoutdent();
        }
    }

    return retval;
}

int ewend(int retval, const char *warnmsg)
{
    int was_interrupted = (_ebegin_col < 0);

    if (!was_interrupted) {
        if (retval == 0)
            _pad_to_badge("ok", _C_BGREEN);
        else {
            _pad_to_badge("**", _C_BYELLOW);
            if (warnmsg) {
                eindent();
                ewarn("WARNING: %s", warnmsg);
                eoutdent();
            }
        }
    } else {
        _ebegin_col = -1;
        if (retval != 0 && warnmsg) {
            eindent();
            ewarn("WARNING: %s", warnmsg);
            eoutdent();
        }
    }

    return retval;
}

void eindent(void)
{
    _indent += EINFO_INDENT_SZ;
    if (_indent > EINFO_INDENT_MAX)
        _indent = EINFO_INDENT_MAX;
}

void eoutdent(void)
{
    _indent -= EINFO_INDENT_SZ;
    if (_indent < 0)
        _indent = 0;
}

void elog_header(const char *fmt, ...)
{
    va_list args;
    printk("\n%s", _C_BWHITE);
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    printk("%s\n\n", _C_RESET);
}

void epanic(const char *subsys, const char *msg)
{
    _indent     = 0;
    _ebegin_col = -1;

    printk("\n%s", _C_BRED);
    printk("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    printk(_C_RESET);

    printk(" %s*%s %sKERNEL PANIC%s: ",
           _C_BRED, _C_RESET, _C_BOLD, _C_RESET);
    if (subsys && subsys[0])
        printk("%s: ", subsys);
    printk("%s\n", msg);

    printk(" %s*%s System halted.  Please reboot.\n",
           _C_BRED, _C_RESET);

    printk("%s", _C_BRED);
    printk("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    printk("%s\n", _C_RESET);

    for (;;)
        __asm__ volatile("cli; hlt");
}
