#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>

static volatile int g_launched = 0;

static void do_launch(void) {
    if (g_launched) return;
    g_launched = 1;

    if (access("/setting/disable_usb_debug", F_OK) == 0) return;

    /* Telemetry log */
    int logfd = open("/setting/zve10_launcher.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (logfd >= 0) {
        char msg[128];
        int n = snprintf(msg, sizeof(msg), "[%d] launcher hook: uid=%d euid=%d\n", (int)getpid(), (int)getuid(), (int)geteuid());
        if (n > 0) write(logfd, msg, n);
        close(logfd);
    }

    if (getuid() != 0 && geteuid() != 0) return;

    /* Single-instance lock without O_NOFOLLOW */
    int lock = open("/tmp/zve10_usb_debug.lock", O_RDWR | O_CREAT, 0666);
    if (lock < 0) {
        lock = open("/setting/zve10_usb_debug.lock", O_RDWR | O_CREAT, 0666);
    }
    if (lock < 0) return;

    if (flock(lock, LOCK_EX | LOCK_NB) < 0) {
        close(lock);
        return;
    }

    if (lock < 3) {
        int inherited = fcntl(lock, F_DUPFD, 3);
        close(lock);
        if (inherited < 0) return;
        lock = inherited;
    }

    pid_t child = fork();
    if (child < 0) { close(lock); return; }
    if (child > 0) {
        close(lock);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
        return;
    }

    if (fork() != 0) _exit(0);
    setsid();

    for (int fd = 0; fd < 256; fd++) {
        if (fd != lock) close(fd);
    }

    int input = open("/dev/null", O_RDONLY);
    if (input >= 0 && input != 0) { dup2(input, 0); close(input); }

    int output = open("/setting/zve10_daemon.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (output >= 0) {
        dup2(output, 1);
        dup2(output, 2);
        if (output > 2) close(output);
    }

    char *argv_zve10[] = {"zve10_usb_debugd.elf", NULL};
    char *argv_a6400[] = {"a6400_usb_debugd.elf", NULL};
    char inherited_lock[48];
    snprintf(inherited_lock, sizeof(inherited_lock), "A6400_LAUNCH_LOCK_FD=%d", lock);
    char *env[] = {"PATH=/usr/bin:/bin:/sbin:/usr/sbin:/setting", inherited_lock, NULL};

    execve("/setting/zve10_usb_debugd.elf", argv_zve10, env);
    execve("/setting/a6400_usb_debugd.elf", argv_a6400, env);

    write(2, "USB debugger execve failed\n", 27);
    _exit(127);
}

void _init(void) {
    do_launch();
}

__attribute__((constructor)) void launch_ctor(void) {
    do_launch();
}
