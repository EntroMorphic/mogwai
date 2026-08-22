/* termios extensions - cfmakeraw, CRTSCTS, B115200 - sit outside strict POSIX,
   and the project CFLAGS set -D_POSIX_C_SOURCE. Re-enable them, before any
   system header is pulled in. Same class of trap as the strdup/-lm findings. */
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE  1

/* devtalk.c — talk to the device over serial. No Python anywhere in this project.
 *
 * Every device measurement in this repo used to go through an ad-hoc pyserial
 * one-liner, which meant the numbers could not be reproduced without the
 * espressif virtualenv. This is the same thing in C and termios.
 *
 * Args are a script, executed left to right:
 *   -r        pulse DTR/RTS to reset the ESP32
 *   -w MS     read for MS milliseconds, printing what arrives
 *   -s TEXT   send TEXT followed by newline
 *   -q        suppress output until after the next -r settles (drops boot log)
 *
 *   devtalk /dev/cu.usbserial-0001 -r -w 6000 -s "turn the lights on" -w 1500
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>

static int fd = -1;

static long now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* Read for ms milliseconds, echoing to stdout. Returns bytes seen. */
static long drain(long ms, int quiet) {
    char buf[1024];
    long end = now_ms() + ms, total = 0;
    while (now_ms() < end) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) {
            total += n;
            if (!quiet) { fwrite(buf, 1, (size_t)n, stdout); fflush(stdout); }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        } else {
            usleep(2000);
        }
    }
    return total;
}

/* The ESP32 auto-reset circuit: DTR low, RTS high (EN low), pause, RTS low. */
static void reset_device(void) {
    int bits = TIOCM_DTR;  ioctl(fd, TIOCMBIC, &bits);
    bits = TIOCM_RTS;      ioctl(fd, TIOCMBIS, &bits);
    usleep(150000);
    bits = TIOCM_RTS;      ioctl(fd, TIOCMBIC, &bits);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s PORT [-r] [-w MS] [-s TEXT] [-q]\n", argv[0]);
        return 1;
    }
    fd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror(argv[1]); return 1; }

    struct termios t;
    if (tcgetattr(fd, &t)) { perror("tcgetattr"); return 1; }
    cfmakeraw(&t);
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cflag |=  (CLOCAL | CREAD);
    t.c_cflag &= ~CRTSCTS;              /* no hardware flow control: RTS is the reset line */
    t.c_cflag &= ~CSTOPB;
    t.c_cflag &= ~PARENB;
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &t)) { perror("tcsetattr"); return 1; }

    int quiet = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) {
            reset_device();
        } else if (!strcmp(argv[i], "-q")) {
            quiet = 1;
        } else if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            drain(atol(argv[++i]), quiet);
            quiet = 0;
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            const char *s = argv[++i];
            if (write(fd, s, strlen(s)) < 0 || write(fd, "\n", 1) < 0) {
                perror("write"); return 1;
            }
            tcdrain(fd);
        } else {
            fprintf(stderr, "devtalk: unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }
    close(fd);
    return 0;
}
