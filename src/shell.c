#include <arch/x86_64/serial.h>

#include <core/scheduler.h>
#include <core/proc.h>

#include <drivers/net/ethernet.h>
#include <drivers/input/kb.h>

#include <fs/vfs.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "shell.h"

static void sdbg_char(char c) {
    serial_write(COM1, c);
}

static void sdbg_str(const char *s) {
    if (!s) return;
    while (*s) serial_write(COM1, *s++);
}

static void sdbg_hex(uint64_t v) {
    char buf[19];
    buf[0]  = '0'; buf[1] = 'x';
    int i;
    for (i = 0; i < 16; i++) {
        uint8_t nibble = (v >> (60 - i*4)) & 0xF;
        buf[2+i] = nibble < 10 ? '0'+nibble : 'a'+(nibble-10);
    }
    buf[18] = '\0';
    sdbg_str(buf);
}

static void sdbg_u64(uint64_t v) {
    if (v == 0) { sdbg_char('0'); return; }
    char buf[21]; int pos = 20; buf[20] = '\0';
    while (v) { buf[--pos] = '0' + (v % 10); v /= 10; }
    sdbg_str(&buf[pos]);
}

#define SDBG(msg)        do { sdbg_str("[SHELL] " msg "\r\n"); } while(0)
#define SDBG2(msg, val)  do { sdbg_str("[SHELL] " msg); sdbg_u64(val); sdbg_str("\r\n"); } while(0)
#define SDBG2X(msg, val) do { sdbg_str("[SHELL] " msg); sdbg_hex(val); sdbg_str("\r\n"); } while(0)

#define SHELL_LINE_MAX   512
#define SHELL_ARGC_MAX   16
#define SHELL_HIST_MAX   32
#define SHELL_INBUF_SIZE 1024                       /* must be power of 2 */
#define SHELL_INBUF_MASK (SHELL_INBUF_SIZE - 1)

#define SHELL_CTRL_C     0x03
#define SHELL_CTRL_L     0x0C

static volatile char     sh_inbuf[SHELL_INBUF_SIZE];
static volatile uint32_t sh_inhead = 0;
static volatile uint32_t sh_intail = 0;

static void sh_push(char c) {
    uint32_t next = (sh_inhead + 1) & SHELL_INBUF_MASK;
    if (next != sh_intail) {           /* drop if full */
        sh_inbuf[sh_inhead] = c;
        sh_inhead = next;
    }
}

static bool sh_pop(char *c) {
    if (sh_intail == sh_inhead) return false;
    *c = sh_inbuf[sh_intail];
    sh_intail = (sh_intail + 1) & SHELL_INBUF_MASK;
    return true;
}

static void shell_kb_callback(kb_event_t *event) {
    if (!event->pressed) return;

    uint8_t kc  = event->keycode;
    uint16_t mod = event->modifiers;

    if (kc == 0) return;

    if (mod & KB_MOD_CTRL) {
        switch (kc | 0x20) {           /* fold to lower */
            case 'c': sh_push(SHELL_CTRL_C); return;
            case 'l': sh_push(SHELL_CTRL_L); return;
        }
    }

    sh_push((char)kc);
}

static char sh_history[SHELL_HIST_MAX][SHELL_LINE_MAX];
static int  sh_hist_count = 0;
static int  sh_hist_idx   = -1;    /* -1 = not browsing */

static void hist_push(const char *line) {
    if (line[0] == '\0') return;
    /* avoid duplicate consecutive entries */
    if (sh_hist_count > 0) {
        int last = (sh_hist_count - 1) % SHELL_HIST_MAX;
        if (strcmp(sh_history[last], line) == 0) return;
    }
    strncpy(sh_history[sh_hist_count % SHELL_HIST_MAX], line, SHELL_LINE_MAX - 1);
    sh_history[sh_hist_count % SHELL_HIST_MAX][SHELL_LINE_MAX - 1] = '\0';
    sh_hist_count++;
}

static char   sh_line[SHELL_LINE_MAX];
static size_t sh_linelen = 0;

static void line_clear(void) {
    memset(sh_line, 0, SHELL_LINE_MAX);
    sh_linelen = 0;
}

static void sh_prompt(void) {
    char cwd[VFS_PATH_MAX];
    if (vfs_getcwd(cwd, sizeof(cwd)) != 0)
        strcpy(cwd, "?");
    printk("\x1b[1;32mspinix\x1b[0m:\x1b[1;34m%s\x1b[0m# ", cwd);
}

static void sh_redraw(void) {
    printk("\r\x1b[2K");    /* carriage-return, erase line */
    sh_prompt();
    printk("%s", sh_line);
}

static int sh_parse(char *line, char **argv, int max) {
    int argc = 0;
    char *p  = line;

    while (*p && argc < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}

static void cmd_help(void) {
    printk(
        "\n\x1b[1mBuilt-in commands\x1b[0m\n"
        "  help                  show this message\n"
        "  clear                 clear screen\n"
        "  pwd                   print working directory\n"
        "  cd [path]             change directory  (default /)\n"
        "  ls [path]             list directory    (default .)\n"
        "  cat <file>            print file contents\n"
        "  echo [args...]        print arguments\n"
        "  mkdir <path>          create directory\n"
        "  rm <file>             remove file\n"
        "  rmdir <dir>           remove empty directory\n"
        "  mv <src> <dst>        rename / move\n"
        "  cp <src> <dst>        copy file\n"
        "  touch <file>          create empty file or update mtime\n"
        "  write <file> <text>   write text to file (overwrites)\n"
        "  append <file> <text>  append text to file\n"
        "  stat <path>           show file metadata\n"
        "  hexdump <file>        hex + ASCII dump\n"
        "  mounts                list mounted filesystems\n"
        "  mem                   heap stats\n"
        "  tasks                 scheduler task list\n"
        "\n"
        "  Ctrl-C  cancel line\n"
        "  Ctrl-L  clear screen\n"
        "  Up/Down history navigation\n\n"
    );
}

static void cmd_clear(void) {
    printk("\x1b[2J\x1b[H");
}

static void cmd_pwd(void) {
    char buf[VFS_PATH_MAX];
    int ret = vfs_getcwd(buf, sizeof(buf));
    if (ret == 0)
        printk("%s\n", buf);
    else
        printk("pwd: error %d\n", ret);
}

static void cmd_cd(int argc, char **argv) {
    const char *path = (argc >= 2) ? argv[1] : "/";
    int ret = vfs_chdir(path);
    if (ret != 0)
        printk("cd: %s: no such directory (err %d)\n", path, ret);
}

static void cmd_ls(int argc, char **argv) {
    const char *path = (argc >= 2) ? argv[1] : ".";

    vfs_file_t *dir = NULL;
    int ret = vfs_open(path, VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &dir);
    if (ret != 0) {
        printk("ls: %s: %d\n", path, ret);
        return;
    }

    vfs_dirent_t *de = (vfs_dirent_t *)kmalloc(sizeof(vfs_dirent_t));
    if (!de) { vfs_close(dir); return; }

    int col = 0;
    while ((ret = vfs_readdir(dir, de)) > 0) {
        const char *suffix = "";
        switch (de->d_type) {
            case VFS_DT_DIR:  suffix = "/"; break;
            case VFS_DT_LNK:  suffix = "@"; break;
            case VFS_DT_CHR:
            case VFS_DT_BLK:  suffix = "%"; break;
            default:          break;
        }
        bool is_dir = (de->d_type == VFS_DT_DIR);

        if (is_dir)
            printk("  \x1b[1;34m%s%s\x1b[0m", de->d_name, suffix);
        else
            printk("  %s%s", de->d_name, suffix);

        int visible = (int)strlen(de->d_name) + (int)strlen(suffix);
        for (int p = visible; p < 24; p++)
            printk(" ");

        col++;
        if (col % 2 == 0) printk("\n");
    }
    if (col % 2 != 0) printk("\n");

    kfree(de);
    vfs_close(dir);
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { printk("usage: cat <file>\n"); return; }

    vfs_file_t *f = NULL;
    int ret = vfs_open(argv[1], VFS_O_RDONLY, 0, &f);
    if (ret != 0) { printk("cat: %s: err %d\n", argv[1], ret); return; }

    char *buf = (char *)kmalloc(4096);
    if (!buf) { vfs_close(f); return; }

    int n;
    while ((n = vfs_read(f, buf, 4095)) > 0) {
        buf[n] = '\0';
        printk("%s", buf);
    }
    printk("\n");

    kfree(buf);
    vfs_close(f);
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) printk(" ");
        printk("%s", argv[i]);
    }
    printk("\n");
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { printk("usage: mkdir <path>\n"); return; }
    int ret = vfs_mkdir(argv[1], 0755);
    if (ret != 0) printk("mkdir: %s: err %d\n", argv[1], ret);
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { printk("usage: rm <file>\n"); return; }
    int ret = vfs_unlink(argv[1]);
    if (ret != 0) printk("rm: %s: err %d\n", argv[1], ret);
}

static void cmd_rmdir(int argc, char **argv) {
    if (argc < 2) { printk("usage: rmdir <dir>\n"); return; }
    int ret = vfs_rmdir(argv[1]);
    if (ret != 0) printk("rmdir: %s: err %d\n", argv[1], ret);
}

static void cmd_mv(int argc, char **argv) {
    if (argc < 3) { printk("usage: mv <src> <dst>\n"); return; }
    int ret = vfs_rename(argv[1], argv[2]);
    if (ret != 0) printk("mv: err %d\n", ret);
}

static void cmd_cp(int argc, char **argv) {
    if (argc < 3) { printk("usage: cp <src> <dst>\n"); return; }

    vfs_file_t *src = NULL, *dst = NULL;
    int ret = vfs_open(argv[1], VFS_O_RDONLY, 0, &src);
    if (ret != 0) { printk("cp: cannot open %s: err %d\n", argv[1], ret); return; }

    ret = vfs_open(argv[2], VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC, 0644, &dst);
    if (ret != 0) {
        printk("cp: cannot create %s: err %d\n", argv[2], ret);
        vfs_close(src);
        return;
    }

    char *buf = (char *)kmalloc(4096);
    if (!buf) { vfs_close(src); vfs_close(dst); return; }

    int n;
    while ((n = vfs_read(src, buf, 4096)) > 0) {
        int written = vfs_write(dst, buf, n);
        if (written < 0) {
            printk("cp: write error %d\n", written);
            break;
        }
    }

    kfree(buf);
    vfs_close(src);
    vfs_close(dst);
}

static void cmd_touch(int argc, char **argv) {
    if (argc < 2) { printk("usage: touch <file>\n"); return; }
    vfs_file_t *f = NULL;
    int ret = vfs_open(argv[1], VFS_O_CREAT | VFS_O_WRONLY, 0644, &f);
    if (ret != 0) { printk("touch: %s: err %d\n", argv[1], ret); return; }
    vfs_close(f);
}

static void cmd_write(int argc, char **argv) {
    if (argc < 3) { printk("usage: write <file> <text...>\n"); return; }

    vfs_file_t *f = NULL;
    int ret = vfs_open(argv[1], VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC, 0644, &f);
    if (ret != 0) { printk("write: %s: err %d\n", argv[1], ret); return; }

    for (int i = 2; i < argc; i++) {
        if (i > 2) vfs_write(f, " ", 1);
        vfs_write(f, argv[i], strlen(argv[i]));
    }
    vfs_write(f, "\n", 1);
    vfs_close(f);
}

static void cmd_append(int argc, char **argv) {
    if (argc < 3) { printk("usage: append <file> <text...>\n"); return; }

    vfs_file_t *f = NULL;
    int ret = vfs_open(argv[1], VFS_O_CREAT | VFS_O_WRONLY | VFS_O_APPEND, 0644, &f);
    if (ret != 0) { printk("append: %s: err %d\n", argv[1], ret); return; }

    for (int i = 2; i < argc; i++) {
        if (i > 2) vfs_write(f, " ", 1);
        vfs_write(f, argv[i], strlen(argv[i]));
    }
    vfs_write(f, "\n", 1);
    vfs_close(f);
}

static void cmd_stat(int argc, char **argv) {
    if (argc < 2) { printk("usage: stat <path>\n"); return; }

    vfs_stat_t st;
    int ret = vfs_stat(argv[1], &st);
    if (ret != 0) { printk("stat: %s: err %d\n", argv[1], ret); return; }

    const char *type = "regular file";
    vfs_file_t *probe = NULL;
    if (vfs_open(argv[1], VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &probe) == 0) {
        vfs_close(probe);
        type = "directory";
    }

    printk("  Path:   %s\n",       argv[1]);
    printk("  Type:   %s\n",       type);
    printk("  Size:   %llu bytes\n", st.st_size);
    printk("  Inode:  %llu\n",     st.st_ino);
    printk("  Links:  %u\n",       st.st_nlink);
    printk("  UID:    %u\n",       st.st_uid);
    printk("  GID:    %u\n",       st.st_gid);
    printk("  Mode:   0%o\n",      st.st_mode);
}

static void cmd_hexdump(int argc, char **argv) {
    if (argc < 2) { printk("usage: hexdump <file>\n"); return; }

    vfs_file_t *f = NULL;
    int ret = vfs_open(argv[1], VFS_O_RDONLY, 0, &f);
    if (ret != 0) { printk("hexdump: %s: err %d\n", argv[1], ret); return; }

    uint8_t *buf = (uint8_t *)kmalloc(256);
    if (!buf) { vfs_close(f); return; }

    uint64_t offset = 0;
    int n;
    while ((n = vfs_read(f, buf, 16)) > 0) {
        printk("  %08llx  ", offset);
        for (int i = 0; i < 16; i++) {
            if (i < n)  printk("%02x ", buf[i]);
            else        printk("   ");
            if (i == 7) printk(" ");
        }
        printk(" |");
        for (int i = 0; i < n; i++)
            printk("%c", (buf[i] >= 0x20 && buf[i] < 0x7F) ? buf[i] : '.');
        printk("|\n");
        offset += n;
    }

    kfree(buf);
    vfs_close(f);
}

static void cmd_mounts(void) {
    vfs_dump_mounts();
}

static void cmd_mem(void) {
    heap_print_stats();
}

static void cmd_tasks(void) {
    scheduler_dump_tasks();
}

static void sh_exec(char *line) {
    /* strip leading whitespace */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0') return;

    hist_push(line);

    char *argv[SHELL_ARGC_MAX];
    int   argc = sh_parse(line, argv, SHELL_ARGC_MAX);
    if (argc == 0) return;

    if      (!strcmp(argv[0], "help"))    cmd_help();
    else if (!strcmp(argv[0], "clear"))   cmd_clear();
    else if (!strcmp(argv[0], "pwd"))     cmd_pwd();
    else if (!strcmp(argv[0], "cd"))      cmd_cd(argc, argv);
    else if (!strcmp(argv[0], "ls"))      cmd_ls(argc, argv);
    else if (!strcmp(argv[0], "cat"))     cmd_cat(argc, argv);
    else if (!strcmp(argv[0], "echo"))    cmd_echo(argc, argv);
    else if (!strcmp(argv[0], "mkdir"))   cmd_mkdir(argc, argv);
    else if (!strcmp(argv[0], "rm"))      cmd_rm(argc, argv);
    else if (!strcmp(argv[0], "rmdir"))   cmd_rmdir(argc, argv);
    else if (!strcmp(argv[0], "mv"))      cmd_mv(argc, argv);
    else if (!strcmp(argv[0], "cp"))      cmd_cp(argc, argv);
    else if (!strcmp(argv[0], "touch"))   cmd_touch(argc, argv);
    else if (!strcmp(argv[0], "write"))   cmd_write(argc, argv);
    else if (!strcmp(argv[0], "append"))  cmd_append(argc, argv);
    else if (!strcmp(argv[0], "stat"))    cmd_stat(argc, argv);
    else if (!strcmp(argv[0], "hexdump")) cmd_hexdump(argc, argv);
    else if (!strcmp(argv[0], "mounts"))  cmd_mounts();
    else if (!strcmp(argv[0], "mem"))     cmd_mem();
    else if (!strcmp(argv[0], "tasks"))   cmd_tasks();
    else
        printk("shell: %s: command not found\n", argv[0]);
}

void shell_task(void) {
    sh_prompt();

    while (1) {
        char c;
        if (!sh_pop(&c)) {
            yield();
            continue;
        }

        uint8_t uc = (uint8_t)c;

        if (uc == SHELL_CTRL_C) {
            printk("^C\n");
            line_clear();
            sh_hist_idx = -1;
            sh_prompt();
            continue;
        }

        if (uc == SHELL_CTRL_L) {
            cmd_clear();
            sh_redraw();
            continue;
        }

        if (uc == KEY_BACKSPACE) {
            if (sh_linelen > 0) {
                sh_linelen--;
                sh_line[sh_linelen] = '\0';
                printk("\b \b");
            }
            continue;
        }

        if (uc == KEY_ENTER) {
            printk("\n");
            sh_line[sh_linelen] = '\0';
            sh_exec(sh_line);
            line_clear();
            sh_hist_idx = -1;
            sh_prompt();
            continue;
        }

        if (uc == KEY_UP) {
            if (sh_hist_count == 0) continue;
            if (sh_hist_idx < 0)
                sh_hist_idx = (sh_hist_count < SHELL_HIST_MAX)
                              ? sh_hist_count - 1
                              : SHELL_HIST_MAX - 1;
            else if (sh_hist_idx > 0)
                sh_hist_idx--;

            strncpy(sh_line,
                    sh_history[sh_hist_idx % SHELL_HIST_MAX],
                    SHELL_LINE_MAX - 1);
            sh_line[SHELL_LINE_MAX - 1] = '\0';
            sh_linelen = strlen(sh_line);
            sh_redraw();
            continue;
        }

        if (uc == KEY_DOWN) {
            if (sh_hist_idx < 0) continue;
            int top = (sh_hist_count < SHELL_HIST_MAX)
                      ? sh_hist_count - 1
                      : SHELL_HIST_MAX - 1;
            if (sh_hist_idx < top) {
                sh_hist_idx++;
                strncpy(sh_line,
                        sh_history[sh_hist_idx % SHELL_HIST_MAX],
                        SHELL_LINE_MAX - 1);
                sh_line[SHELL_LINE_MAX - 1] = '\0';
                sh_linelen = strlen(sh_line);
            } else {
                sh_hist_idx = -1;
                line_clear();
            }
            sh_redraw();
            continue;
        }

        if (uc >= 0x20 && uc < 0x7F) {
            if (sh_linelen < SHELL_LINE_MAX - 1) {
                sh_line[sh_linelen++] = c;
                printk("%c", c);
            }
            continue;
        }
    }
}

void shell_init(void) {
    kb_set_callback(shell_kb_callback);
    proc_create("shell", shell_task, PRIORITY_NORMAL);
}
