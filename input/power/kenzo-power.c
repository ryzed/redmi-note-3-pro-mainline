/*
 * kenzo-power — кнопка питания Redmi Note 3 как на телефоне.
 *
 * Короткое нажатие гасит экран, следующее — зажигает; система при этом
 * остаётся включённой, Wi-Fi и ssh живут. Удержание ≈0,8 с открывает меню
 * jwm, где есть «Выключить» и «Перезагрузить».
 *
 * Демон забирает pm8941_pwrkey себе (EVIOCGRAB через libevdev), поэтому X
 * не получает XF86PowerOff, а logind — KEY_POWER. Спящего режима на этом
 * ядре нет (/sys/power/state пуст), «погасить экран» — это DPMS.
 *
 * Сборка: make   (нужны gcc, musl-dev, libevdev-dev, libx11-dev, libxext-dev,
 *                 libxtst-dev)
 * Запуск: kenzo-power [-d устройство] [-v]
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include <libevdev-1.0/libevdev/libevdev.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/dpms.h>

/*
 * Постоянное имя кнопки из правила udev 99-kenzo-power.rules: номер eventN
 * зависит от порядка, в котором ядро нашло устройства, и может съехать.
 */
#define DEVICE "/dev/input/kenzo-pwrkey"

/*
 * Состояние панели. Берём из sysfs, а не у X: xset q и после пробуждения
 * продолжает говорить «Monitor is Off» (проверено 05.11), а атрибут
 * панели меняется вместе с подсветкой.
 */
#define PANEL_DPMS "/sys/class/drm/card0-DSI-1/dpms"

#define HOLD_MS 800      /* столько держать кнопку — открыть меню */

struct screen {
    Display *display;
    int dpms;           /* есть ли в X расширение DPMS */
};

static volatile sig_atomic_t stopping = 0;
static int verbose = 0;

/* Отметка действия в журнале: видно, что демон распознал. */
static void say(const char *what)
{
    if (verbose) {
        fprintf(stderr, "%s\n", what);
    }
}

static void on_signal(int number)
{
    (void)number;
    stopping = 1;
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Экран погашен? */
static int screen_is_off(void)
{
    char buf[16];
    int fd = open(PANEL_DPMS, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';
    return strncmp(buf, "Off", 3) == 0;
}

/* Погасить или зажечь экран. */
static void screen_set(struct screen *screen, int on)
{
    if (screen->dpms) {
        DPMSForceLevel(screen->display, on ? DPMSModeOn : DPMSModeOff);
        XFlush(screen->display);
    }
}

/* Открыть корневое меню jwm: оно висит на клавише Super_L. */
static void open_menu(struct screen *screen)
{
    KeyCode code = XKeysymToKeycode(screen->display, XK_Super_L);
    if (code == 0) {
        return;
    }
    XTestFakeKeyEvent(screen->display, code, True, CurrentTime);
    XTestFakeKeyEvent(screen->display, code, False, CurrentTime);
    XFlush(screen->display);
}

/*
 * Решение принимается по отпусканию: только тогда известно, коротким было
 * нажатие или долгим.
 */
static void act(struct screen *screen, long long held)
{
    if (screen_is_off()) {
        /* С погашенного экрана любое нажатие только будит. */
        screen_set(screen, 1);
        say("пробуждение");
        return;
    }
    if (held >= HOLD_MS) {
        open_menu(screen);
        say("меню");
    } else {
        screen_set(screen, 0);
        say("экран погашен");
    }
}

int main(int argc, char **argv)
{
    const char *device = DEVICE;
    int option;

    while ((option = getopt(argc, argv, "d:v")) != -1) {
        switch (option) {
        case 'd':
            device = optarg;
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            fprintf(stderr, "использование: %s [-d устройство] [-v]\n", argv[0]);
            return 2;
        }
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    int fd = open(device, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "не открыть %s: %s\n", device, strerror(errno));
        return 1;
    }

    struct libevdev *evdev = NULL;
    if (libevdev_new_from_fd(fd, &evdev) < 0) {
        fprintf(stderr, "libevdev не принял %s\n", device);
        close(fd);
        return 1;
    }
    /* Без KEY_POWER это не кнопка питания: чужое устройство не забираем. */
    if (!libevdev_has_event_code(evdev, EV_KEY, KEY_POWER)) {
        fprintf(stderr, "%s — не кнопка питания (%s)\n", device,
                libevdev_get_name(evdev));
        libevdev_free(evdev);
        close(fd);
        return 1;
    }
    if (libevdev_grab(evdev, LIBEVDEV_GRAB) < 0) {
        fprintf(stderr, "не удалось забрать устройство: %s\n", strerror(errno));
        libevdev_free(evdev);
        close(fd);
        return 1;
    }
    if (verbose) {
        fprintf(stderr, "взял %s (%s)\n", device, libevdev_get_name(evdev));
    }

    struct screen screen;
    memset(&screen, 0, sizeof(screen));
    screen.display = XOpenDisplay(NULL);
    if (screen.display == NULL) {
        fprintf(stderr, "не открыть дисплей X (DISPLAY=%s)\n",
                getenv("DISPLAY") ? getenv("DISPLAY") : "не задан");
        libevdev_grab(evdev, LIBEVDEV_UNGRAB);
        libevdev_free(evdev);
        close(fd);
        return 1;
    }
    int event_base = 0;
    int error_base = 0;
    int major = 0;
    int minor = 0;
    if (!XTestQueryExtension(screen.display, &event_base, &error_base,
                             &major, &minor)) {
        fprintf(stderr, "в X нет расширения XTest\n");
        XCloseDisplay(screen.display);
        libevdev_grab(evdev, LIBEVDEV_UNGRAB);
        libevdev_free(evdev);
        close(fd);
        return 1;
    }
    screen.dpms = DPMSQueryExtension(screen.display, &event_base, &error_base);
    if (!screen.dpms) {
        fprintf(stderr, "в X нет расширения DPMS\n");
    }

    int pressed = 0;
    long long press_ms = 0;

    while (!stopping) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);

        int ready = select(fd + 1, &set, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "select: %s\n", strerror(errno));
            break;
        }

        struct input_event event;
        int rc;
        while ((rc = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL,
                                         &event)) == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (event.type != EV_KEY || event.code != KEY_POWER) {
                continue;
            }
            if (event.value == 1) {
                pressed = 1;
                press_ms = now_ms();
            } else if (event.value == 0 && pressed) {
                pressed = 0;
                act(&screen, now_ms() - press_ms);
            }
        }
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            /* Состояние устройства после SYN_DROPPED нам не нужно:
             * пропускаем синхронизацию до конца. События синхронизации
             * приходят кодом SYNC, конец — -EAGAIN. */
            while (libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_SYNC,
                                       &event) == LIBEVDEV_READ_STATUS_SYNC) {
            }
        }
    }

    if (verbose) {
        fprintf(stderr, "выхожу\n");
    }
    XCloseDisplay(screen.display);
    libevdev_grab(evdev, LIBEVDEV_UNGRAB);
    libevdev_free(evdev);
    close(fd);
    return 0;
}
