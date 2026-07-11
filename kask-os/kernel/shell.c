#include "../shared/kernel.h"

/* ============================================================
 * Kask OS — Unix-Like Shell with Directory Tree Filesystem
 * ============================================================ */

#define MAX_CMD_LEN   256
#define MAX_ARGS      16
#define MAX_NODES     128
#define MAX_NAME      64
#define MAX_DATA      1024
#define MAX_CHILDREN  32

/* ---- Filesystem Node ---- */
typedef struct fs_node {
    char name[MAX_NAME];
    int is_dir;
    char data[MAX_DATA];
    int data_len;
    struct fs_node* children[MAX_CHILDREN];
    int child_count;
    struct fs_node* parent;
    int used;
} fs_node_t;

/* ---- Static node pool (no malloc) ---- */
static fs_node_t node_pool[MAX_NODES];
static int next_node = 0;
static fs_node_t* root = 0;
static fs_node_t* cwd = 0;
static unsigned int tick_counter = 0;

/* ---- Color codes ---- */
#define C_PROMPT  0x0A
#define C_NORMAL  0x0F
#define C_HEAD    0x0B
#define C_ERROR   0x0C
#define C_ACCENT  0x0E
#define C_DIM     0x07
#define C_DIR     0x09

static void cprint(const char* s, uint8_t c) {
    uint8_t p = hw_get_color();
    hw_set_color(c);
    hw_print(s);
    hw_set_color(p);
}

/* ---- Allocate a node ---- */
static fs_node_t* alloc_node(const char* name, int is_dir, fs_node_t* parent) {
    if (next_node >= MAX_NODES) return 0;
    fs_node_t* n = &node_pool[next_node++];
    k_memset(n, 0, sizeof(fs_node_t));
    k_strncpy(n->name, name, MAX_NAME - 1);
    n->is_dir = is_dir;
    n->parent = parent;
    n->used = 1;
    if (parent && parent->child_count < MAX_CHILDREN) {
        parent->children[parent->child_count++] = n;
    }
    return n;
}

/* ---- Set file data ---- */
static void set_data(fs_node_t* n, const char* data) {
    int len = k_strlen(data);
    if (len >= MAX_DATA) len = MAX_DATA - 1;
    k_memcpy(n->data, data, len);
    n->data[len] = '\0';
    n->data_len = len;
}

/* ---- Initialize filesystem ---- */
static void fs_init(void) {
    k_memset(node_pool, 0, sizeof(node_pool));
    next_node = 0;

    root = alloc_node("/", 1, 0);
    root->parent = root;

    fs_node_t* etc = alloc_node("etc", 1, root);
    fs_node_t* proc = alloc_node("proc", 1, root);
    fs_node_t* home = alloc_node("home", 1, root);
    fs_node_t* tmp = alloc_node("tmp", 1, root);
    fs_node_t* bin = alloc_node("bin", 1, root);
    fs_node_t* user = alloc_node("user", 1, home);

    fs_node_t* f;
    f = alloc_node("hostname", 0, etc);
    set_data(f, "kask-os");
    f = alloc_node("motd", 0, etc);
    set_data(f, "Welcome to Kask OS!\nType 'help' for available commands.\n");
    f = alloc_node("version", 0, etc);
    set_data(f, "Kask OS v1.0.0 (64-bit kernel, Jul 2026)");
    f = alloc_node("passwd", 0, etc);
    set_data(f, "root:x:0:0:root:/root:/bin/sh\nuser:x:1000:1000:user:/home/user:/bin/sh");

    f = alloc_node("cpuinfo", 0, proc);
    set_data(f, "processor : 0\nvendor    : KaskOS\nmodel     : Virtual CPU\narch      : multi-arch (x86_64/arm64/riscv64)");
    f = alloc_node("meminfo", 0, proc);
    set_data(f, "MemTotal:  Unknown\nMemFree:   Unknown\nNote: memory detection not yet implemented");

    f = alloc_node("readme.txt", 0, user);
    set_data(f, "Welcome to Kask OS!\nThis is a 64-bit multi-architecture operating system\nthat boots on x86_64, ARM64, and RISC-V platforms.\nType 'help' for available commands.");

    (void)tmp; (void)bin;
    cwd = root;
}

/* ---- Find child by name ---- */
static fs_node_t* find_child(fs_node_t* dir, const char* name) {
    if (!dir || !dir->is_dir) return 0;
    for (int i = 0; i < dir->child_count; i++) {
        if (dir->children[i]->used && k_strcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    }
    return 0;
}

/* ---- Resolve a path (absolute or relative) ---- */
static fs_node_t* resolve_path(const char* path) {
    if (!path || !path[0]) return cwd;

    fs_node_t* cur;
    if (path[0] == '/') {
        cur = root;
        path++;
    } else {
        cur = cwd;
    }

    char token[MAX_NAME];
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;

        int i = 0;
        while (*path && *path != '/' && i < MAX_NAME - 1)
            token[i++] = *path++;
        token[i] = '\0';

        if (k_strcmp(token, ".") == 0) continue;
        if (k_strcmp(token, "..") == 0) {
            cur = cur->parent ? cur->parent : root;
            continue;
        }

        fs_node_t* child = find_child(cur, token);
        if (!child) return 0;
        cur = child;
    }
    return cur;
}

/* ---- Get absolute path of a node ---- */
static void get_path(fs_node_t* node, char* buf, int bufsize) {
    if (node == root) { k_strcpy(buf, "/"); return; }

    char tmp[256];
    buf[0] = '\0';
    fs_node_t* n = node;
    while (n && n != root) {
        k_strcpy(tmp, "/");
        k_strcat(tmp, n->name);
        k_strcat(tmp, buf);
        k_strcpy(buf, tmp);
        n = n->parent;
    }
    if (buf[0] == '\0') k_strcpy(buf, "/");
}

/* ---- Read line ---- */
static int read_line(char* buf, int max) {
    int pos = 0;
    k_memset(buf, 0, max);
    while (1) {
        char c = hw_getchar();
        tick_counter++;
        if (c == '\n') { buf[pos] = '\0'; hw_putchar('\n'); return pos; }
        if (c == '\b') {
            if (pos > 0) { pos--; buf[pos] = '\0'; hw_putchar('\b'); }
        } else if (c >= ' ' && pos < max - 1) {
            buf[pos++] = c; hw_putchar(c);
        }
    }
}

/* ---- Parse args ---- */
static int parse_args(char* line, char* argv[], int max) {
    int argc = 0, i = 0;
    while (line[i] && argc < max) {
        while (line[i] == ' ') i++;
        if (!line[i]) break;
        argv[argc++] = &line[i];
        while (line[i] && line[i] != ' ') i++;
        if (line[i]) { line[i] = '\0'; i++; }
    }
    return argc;
}

/* ============================================================
 * Command Implementations
 * ============================================================ */

static void cmd_help(void) {
    cprint("\n  Kask OS Shell Commands:\n", C_HEAD);
    cprint("  ----------------------------------------\n", C_DIM);
    const char* cmds[][2] = {
        {"help       ", "Show this help message"},
        {"clear      ", "Clear the screen"},
        {"echo <msg> ", "Print a message"},
        {"uname [-a] ", "Show system information"},
        {"version    ", "Show OS version"},
        {"whoami     ", "Show current user"},
        {"hostname   ", "Show hostname"},
        {"uptime     ", "Show uptime (approx)"},
        {"date       ", "Show current date"},
        {"pwd        ", "Print working directory"},
        {"cd <dir>   ", "Change directory"},
        {"ls [-l]    ", "List directory contents"},
        {"dir        ", "Alias for ls"},
        {"tree       ", "Show directory tree"},
        {"cat <file> ", "Display file contents"},
        {"touch <f>  ", "Create an empty file"},
        {"mkdir <d>  ", "Create a directory"},
        {"rm <file>  ", "Remove a file"},
        {"rmdir <d>  ", "Remove empty directory"},
        {"fsinfo     ", "Show filesystem driver status"},
        {"reboot     ", "Reboot the system"},
        {"shutdown   ", "Halt the system"},
        {0, 0}
    };
    for (int i = 0; cmds[i][0]; i++) {
        hw_set_color(C_ACCENT);
        hw_print("  ");
        hw_print(cmds[i][0]);
        hw_set_color(C_DIM);
        hw_print(cmds[i][1]);
        hw_putchar('\n');
    }
    cprint("  ----------------------------------------\n\n", C_DIM);
    hw_set_color(C_NORMAL);
}

static void cmd_fsinfo(void) {
    cprint("\n  Filesystem Drivers\n", C_HEAD);
    cprint("  ----------------------------------------\n", C_DIM);

    hw_set_color(C_ACCENT); hw_print("  FAT32  ");
    hw_set_color(C_NORMAL); hw_print("driver");
    hw_set_color(0x0A);     hw_print(" [LOADED]\n");
    hw_set_color(C_DIM);
    hw_print("    Supports: FAT32 volumes, 8.3 + LFN filenames\n");
    hw_print("    Status  : Buffer-based parser ready\n");
    hw_print("    Note    : Requires ATA/AHCI block driver for live disk access\n");

    hw_set_color(C_ACCENT); hw_print("  exFAT  ");
    hw_set_color(C_NORMAL); hw_print("driver");
    hw_set_color(0x0A);     hw_print(" [LOADED]\n");
    hw_set_color(C_DIM);
    hw_print("    Supports: exFAT volumes, UTF-16LE filenames, 64-bit file sizes\n");
    hw_print("    Status  : Buffer-based parser ready\n");
    hw_print("    Note    : Requires ATA/AHCI block driver for live disk access\n");

    cprint("  ----------------------------------------\n\n", C_DIM);
    hw_set_color(C_NORMAL);
}

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        hw_print(argv[i]);
        if (i < argc - 1) hw_putchar(' ');
    }
    hw_putchar('\n');
}

static void cmd_uname(int argc, char* argv[]) {
    if (argc > 1 && k_strcmp(argv[1], "-a") == 0) {
        cprint("Kask OS", C_ACCENT);
        hw_print(" kask-os 1.0.0 ");
        cprint("x86_64/arm64/riscv64", C_HEAD);
        hw_print(" 64-bit Multi-Arch Kernel\n");
    } else {
        cprint("Kask OS\n", C_ACCENT);
    }
}

static void cmd_pwd(void) {
    char buf[256];
    get_path(cwd, buf, 256);
    hw_print(buf);
    hw_putchar('\n');
}

static void cmd_cd(int argc, char* argv[]) {
    if (argc < 2) { cwd = root; return; }
    if (k_strcmp(argv[1], "~") == 0 || k_strcmp(argv[1], "") == 0) { cwd = root; return; }
    fs_node_t* target = resolve_path(argv[1]);
    if (!target) {
        cprint("cd: ", C_ERROR); cprint(argv[1], C_ERROR);
        cprint(": No such directory\n", C_ERROR); return;
    }
    if (!target->is_dir) {
        cprint("cd: ", C_ERROR); cprint(argv[1], C_ERROR);
        cprint(": Not a directory\n", C_ERROR); return;
    }
    cwd = target;
}

static void cmd_ls(int argc, char* argv[]) {
    int long_fmt = 0;
    const char* path = 0;
    for (int i = 1; i < argc; i++) {
        if (k_strcmp(argv[i], "-l") == 0) long_fmt = 1;
        else path = argv[i];
    }
    fs_node_t* dir = path ? resolve_path(path) : cwd;
    if (!dir || !dir->is_dir) {
        cprint("ls: cannot access directory\n", C_ERROR); return;
    }
    if (dir->child_count == 0) return;

    for (int i = 0; i < dir->child_count; i++) {
        fs_node_t* c = dir->children[i];
        if (!c->used) continue;
        if (long_fmt) {
            hw_set_color(C_DIM);
            hw_print(c->is_dir ? "drwxr-xr-x  " : "-rw-r--r--  ");
            char sz[16];
            k_itoa(c->is_dir ? c->child_count : c->data_len, sz, 10);
            int pad = 6 - k_strlen(sz);
            while (pad-- > 0) hw_putchar(' ');
            hw_print(sz);
            hw_print("  ");
        }
        hw_set_color(c->is_dir ? C_DIR : C_NORMAL);
        hw_print(c->name);
        if (c->is_dir) hw_putchar('/');
        hw_putchar(long_fmt ? '\n' : ' ');
    }
    if (!long_fmt) hw_putchar('\n');
    hw_set_color(C_NORMAL);
}

static void print_tree(fs_node_t* node, char* prefix, int is_last) {
    hw_set_color(C_DIM);
    hw_print(prefix);
    hw_print(is_last ? "`-- " : "|-- ");
    hw_set_color(node->is_dir ? C_DIR : C_NORMAL);
    hw_print(node->name);
    if (node->is_dir) hw_putchar('/');
    hw_putchar('\n');

    if (node->is_dir) {
        char new_prefix[256];
        k_strcpy(new_prefix, prefix);
        k_strcat(new_prefix, is_last ? "    " : "|   ");
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i]->used)
                print_tree(node->children[i], new_prefix, i == node->child_count - 1);
        }
    }
}

static void cmd_tree(int argc, char* argv[]) {
    fs_node_t* dir = (argc > 1) ? resolve_path(argv[1]) : cwd;
    if (!dir || !dir->is_dir) { cprint("tree: not a directory\n", C_ERROR); return; }
    cprint(dir == root ? "/\n" : "", C_DIR);
    if (dir == root) {
        hw_set_color(C_DIR);
        hw_print("/\n");
    } else {
        hw_set_color(C_DIR);
        hw_print(dir->name);
        hw_print("/\n");
    }
    for (int i = 0; i < dir->child_count; i++) {
        if (dir->children[i]->used)
            print_tree(dir->children[i], "", i == dir->child_count - 1);
    }
}

static void cmd_cat(int argc, char* argv[]) {
    if (argc < 2) { cprint("cat: missing operand\n", C_ERROR); return; }
    fs_node_t* f = resolve_path(argv[1]);
    if (!f) { cprint("cat: ", C_ERROR); cprint(argv[1], C_ERROR); cprint(": No such file\n", C_ERROR); return; }
    if (f->is_dir) { cprint("cat: ", C_ERROR); cprint(argv[1], C_ERROR); cprint(": Is a directory\n", C_ERROR); return; }
    hw_print(f->data);
    if (f->data_len > 0 && f->data[f->data_len - 1] != '\n') hw_putchar('\n');
}

static void cmd_touch(int argc, char* argv[]) {
    if (argc < 2) { cprint("touch: missing operand\n", C_ERROR); return; }
    fs_node_t* existing = find_child(cwd, argv[1]);
    if (existing) return; /* file already exists, just touch it */
    fs_node_t* f = alloc_node(argv[1], 0, cwd);
    if (!f) cprint("touch: cannot create file (pool full)\n", C_ERROR);
}

static void cmd_mkdir(int argc, char* argv[]) {
    if (argc < 2) { cprint("mkdir: missing operand\n", C_ERROR); return; }
    if (find_child(cwd, argv[1])) {
        cprint("mkdir: ", C_ERROR); cprint(argv[1], C_ERROR);
        cprint(": already exists\n", C_ERROR); return;
    }
    fs_node_t* d = alloc_node(argv[1], 1, cwd);
    if (!d) cprint("mkdir: cannot create directory (pool full)\n", C_ERROR);
    else if (d->parent) d->parent = cwd;
}

static void cmd_rm(int argc, char* argv[]) {
    if (argc < 2) { cprint("rm: missing operand\n", C_ERROR); return; }
    fs_node_t* f = find_child(cwd, argv[1]);
    if (!f) { cprint("rm: ", C_ERROR); cprint(argv[1], C_ERROR); cprint(": No such file\n", C_ERROR); return; }
    if (f->is_dir) { cprint("rm: ", C_ERROR); cprint(argv[1], C_ERROR); cprint(": Is a directory (use rmdir)\n", C_ERROR); return; }
    f->used = 0;
    /* Remove from parent's children array */
    for (int i = 0; i < cwd->child_count; i++) {
        if (cwd->children[i] == f) {
            for (int j = i; j < cwd->child_count - 1; j++)
                cwd->children[j] = cwd->children[j + 1];
            cwd->child_count--;
            break;
        }
    }
}

static void cmd_rmdir(int argc, char* argv[]) {
    if (argc < 2) { cprint("rmdir: missing operand\n", C_ERROR); return; }
    fs_node_t* d = find_child(cwd, argv[1]);
    if (!d) { cprint("rmdir: ", C_ERROR); cprint(argv[1], C_ERROR); cprint(": No such directory\n", C_ERROR); return; }
    if (!d->is_dir) { cprint("rmdir: ", C_ERROR); cprint(argv[1], C_ERROR); cprint(": Not a directory\n", C_ERROR); return; }
    if (d->child_count > 0) { cprint("rmdir: ", C_ERROR); cprint(argv[1], C_ERROR); cprint(": Directory not empty\n", C_ERROR); return; }
    d->used = 0;
    for (int i = 0; i < cwd->child_count; i++) {
        if (cwd->children[i] == d) {
            for (int j = i; j < cwd->child_count - 1; j++)
                cwd->children[j] = cwd->children[j + 1];
            cwd->child_count--;
            break;
        }
    }
}

static void cmd_reboot(void) {
    cprint("Rebooting...\n", C_ACCENT);
#if defined(__x86_64__) || defined(__i386__)
    uint8_t good = 0x02;
    while (good & 0x02) __asm__ volatile("inb $0x64, %0" : "=a"(good));
    __asm__ volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));
#endif
    while (1) { __asm__ volatile(""); }
}

static void cmd_shutdown(void) {
    cprint("\n  System halted.\n  You may now turn off your computer.\n\n", C_ACCENT);
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("cli; hlt");
#endif
    while (1) { __asm__ volatile(""); }
}

/* ============================================================
 * Shell Main Loop
 * ============================================================ */

void shell_run(void) {
    char cmd_buf[MAX_CMD_LEN];
    char* argv[MAX_ARGS];

    fs_init();

    cprint("Type ", C_DIM);
    cprint("help", C_ACCENT);
    cprint(" for a list of commands.\n\n", C_DIM);

    while (1) {
        char path_buf[256];
        get_path(cwd, path_buf, 256);

        cprint("root", C_ACCENT);
        cprint("@", C_DIM);
        cprint("kask-os", C_HEAD);
        cprint(":", C_DIM);
        cprint(path_buf, C_DIR);
        cprint("$ ", C_NORMAL);

        int len = read_line(cmd_buf, MAX_CMD_LEN);
        if (len == 0) continue;

        int argc = parse_args(cmd_buf, argv, MAX_ARGS);
        if (argc == 0) continue;

        if (k_strcmp(argv[0], "help") == 0)          cmd_help();
        else if (k_strcmp(argv[0], "clear") == 0)    hw_clear_screen();
        else if (k_strcmp(argv[0], "echo") == 0)     cmd_echo(argc, argv);
        else if (k_strcmp(argv[0], "uname") == 0)    cmd_uname(argc, argv);
        else if (k_strcmp(argv[0], "version") == 0) {
            fs_node_t* f = resolve_path("/etc/version");
            if (f) { cprint(f->data, C_ACCENT); hw_putchar('\n'); }
        }
        else if (k_strcmp(argv[0], "whoami") == 0)   hw_print("root\n");
        else if (k_strcmp(argv[0], "hostname") == 0) {
            fs_node_t* f = resolve_path("/etc/hostname");
            if (f) { hw_print(f->data); hw_putchar('\n'); }
        }
        else if (k_strcmp(argv[0], "uptime") == 0) {
            char buf[32]; hw_print("up ~");
            k_itoa(tick_counter / 10, buf, 10);
            cprint(buf, C_ACCENT); hw_print(" seconds (approx)\n");
        }
        else if (k_strcmp(argv[0], "date") == 0)     hw_print("Thu Jul 10 2026 (no RTC)\n");
        else if (k_strcmp(argv[0], "pwd") == 0)      cmd_pwd();
        else if (k_strcmp(argv[0], "cd") == 0)       cmd_cd(argc, argv);
        else if (k_strcmp(argv[0], "ls") == 0)       cmd_ls(argc, argv);
        else if (k_strcmp(argv[0], "dir") == 0)      cmd_ls(argc, argv);
        else if (k_strcmp(argv[0], "tree") == 0)     cmd_tree(argc, argv);
        else if (k_strcmp(argv[0], "cat") == 0)      cmd_cat(argc, argv);
        else if (k_strcmp(argv[0], "touch") == 0)    cmd_touch(argc, argv);
        else if (k_strcmp(argv[0], "mkdir") == 0)    cmd_mkdir(argc, argv);
        else if (k_strcmp(argv[0], "rm") == 0)       cmd_rm(argc, argv);
        else if (k_strcmp(argv[0], "rmdir") == 0)    cmd_rmdir(argc, argv);
        else if (k_strcmp(argv[0], "reboot") == 0)   cmd_reboot();
        else if (k_strcmp(argv[0], "shutdown") == 0)  cmd_shutdown();
        else if (k_strcmp(argv[0], "fsinfo") == 0)    cmd_fsinfo();
        else { cprint(argv[0], C_ERROR); cprint(": command not found\n", C_ERROR); }
    }
}
