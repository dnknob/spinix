#ifndef SPINIX_LOG_H
#define SPINIX_LOG_H

#include <video/printk.h>

#define EINFO_COLS        80

#define EINFO_INDENT_SZ   2

#define EINFO_INDENT_MAX  40

#define _C_RESET    "\x1b[0m"
#define _C_BOLD     "\x1b[1m"
#define _C_RED      "\x1b[0;31m"
#define _C_YELLOW   "\x1b[0;33m"
#define _C_GREEN    "\x1b[0;32m"
#define _C_CYAN     "\x1b[0;36m"
#define _C_WHITE    "\x1b[0;37m"
#define _C_BRED     "\x1b[1;31m"   /* bold red    */
#define _C_BYELLOW  "\x1b[1;33m"   /* bold yellow */
#define _C_BGREEN   "\x1b[1;32m"   /* bold green  */
#define _C_BWHITE   "\x1b[1;37m"   /* bold white  */
#define _C_BCYAN    "\x1b[1;36m"   /* bold cyan   */

/*
 * einfo / ewarn / eerror
 *
 * Print a complete line:  " <*> [indent]message\n"
 *
 *   einfo  — bold-green  asterisk
 *   ewarn  — bold-yellow asterisk
 *   eerror — bold-red    asterisk
 *
 * These never print a badge.  They are for standalone informational,
 * warning, and error lines.
 */
void einfo (const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ewarn (const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void eerror(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/*
 * ebegin(fmt, ...)
 *
 * Open a two-phase status line:
 *
 *   " * [indent]Starting foo ..."
 *
 * Does NOT print a newline.  Must be followed by eend() or ewend().
 */
void ebegin(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/*
 * eend(retval, errmsg)
 *
 * Close a line opened by ebegin().  Pads to EINFO_COLS then prints:
 *
 *   [ ok ]   (bold green)  — retval == 0
 *   [ !! ]   (bold red)    — retval != 0
 *
 * If retval != 0 AND errmsg is not NULL, an additional eerror() line
 * is printed below the badge
 *
 *   ebegin("Starting rtl8139");
 *   eend(err, "rtl8139: device not found");
 *
 *    * Starting rtl8139 ...                          [ !! ]
 *      * ERROR: rtl8139: device not found
 *
 * Pass NULL for errmsg to suppress the follow-up error line.
 * Returns retval unchanged.
 */
int eend (int retval, const char *errmsg);

/*
 * ewend(retval, warnmsg)
 *
 * Like eend() but uses [ ** ] (bold yellow) on failure.
 * Use for non-fatal conditions where a warning is more appropriate
 * than a hard error.  Prints an ewarn() line when retval != 0.
 *
 *   ebegin("Checking network link");
 *   ewend(no_link, "no link detected, continuing anyway");
 *
 *    * Checking network link ...                     [ ** ]
 *      * WARNING: no link detected, continuing anyway
 */
int ewend(int retval, const char *warnmsg);

/*
 * eindent / eoutdent
 *
 * Increase/decrease the indent level for all subsequent einfo/ewarn/
 * eerror/ebegin calls.  Each level is EINFO_INDENT_SZ spaces.
 * Maximum total indent is EINFO_INDENT_MAX.
 *
 * Canonical usage
 *
 *   ebegin("Starting AHCI storage driver");
 *   eend(ahci_init(), "ahci: probe failed");
 *   eindent();
 *       veinfo("Registered sda (%llu MB, SATA)", mb);
 *       veinfo("Registered sdb (%llu MB, SATA)", mb2);
 *   eoutdent();
 */
void eindent (void);
void eoutdent(void);

/*
 * elog_header(msg)
 *
 * Print a bold-white header line
 *
 * No asterisk prefix, no badge, just a prominent separator.
 */
void elog_header(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/*
 * epanic(subsys, msg)
 *
 *   !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *    * KERNEL PANIC: vmm: out of memory during lazy allocation
 *    * System halted.  Please reboot.
 *   !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 */
void epanic(const char *subsys, const char *msg) __attribute__((noreturn));

/*
 * Use veinfo/vewarn/veerror/vebegin for noisy detail lines that you
 * want suppressed during normal boot but visible when debugging.
 *
 *   veinfo("ahci: port %u: %llu MB, model %s", port, mb, model);
 *   vebegin("Probing AHCI port %u", port);
 *   veend(err, "port timed out");
 */
#ifdef EINFO_VERBOSE
#  define veinfo(...)   einfo(__VA_ARGS__)
#  define vewarn(...)   ewarn(__VA_ARGS__)
#  define veerror(...)  eerror(__VA_ARGS__)
#  define vebegin(...)  ebegin(__VA_ARGS__)
#  define veend(r, m)   eend((r), (m))
#  define vewend(r, m)  ewend((r), (m))
#else
#  define veinfo(...)   do {} while (0)
#  define vewarn(...)   do {} while (0)
#  define veerror(...)  do {} while (0)
#  define vebegin(...)  do {} while (0)
#  define veend(r, m)   (r)
#  define vewend(r, m)  (r)
#endif

#endif