/*
 * vtouch — виртуальный тачскрин для проверки kenzo-touch на телефоне.
 *
 * Создаёт uinput-устройство "kenzo-test-touch" (0..1023 по обеим осям, как у
 * настоящего, 10 слотов), ждёт 3 с, пока демон его откроет, и проигрывает
 * сценарий. Координаты — сырые единицы тачскрина: x = px·1023/1079,
 * y = py·1023/1919.
 *
 * Команды сценария, по одной в строке:
 *   d S ID X Y        палец в слот S с номером касания ID
 *   m S X Y           сдвинуть палец
 *   g S X Y N MS      плавно довести палец до X Y за N кадров, MS мс между ними
 *   g2 DX DY N MS     сдвинуть пальцы в слотах 0 и 1 вместе на DX DY
 *   u S               оторвать палец
 *   s                 конец кадра (SYN_REPORT)
 *   f N               N кадров дрожания пальца 0 без пауз — переполнить
 *                     очередь демона, пока он остановлен (SYN_DROPPED)
 *   w MS              пауза
 *   x команда         выполнить в shell и показать вывод
 *   # …               комментарий
 *
 * Запускать от root (нужен /dev/uinput) — это делает run.sh.
 */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_SLOTS 10

static int fd;
static int pos_x[MAX_SLOTS];
static int pos_y[MAX_SLOTS];
static int active;

static void emit(int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) {
        perror("write");
    }
}

static void abs_setup(int code, int min, int max)
{
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof(a));
    a.code = code;
    a.absinfo.minimum = min;
    a.absinfo.maximum = max;
    ioctl(fd, UI_ABS_SETUP, &a);
}

static void move(int slot, int x, int y)
{
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    pos_x[slot] = x;
    pos_y[slot] = y;
}

static void frame(void)
{
    emit(EV_SYN, SYN_REPORT, 0);
}

static void create_device(void)
{
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    abs_setup(ABS_X, 0, 1023);
    abs_setup(ABS_Y, 0, 1023);
    abs_setup(ABS_MT_SLOT, 0, MAX_SLOTS - 1);
    abs_setup(ABS_MT_TRACKING_ID, 0, 65535);
    abs_setup(ABS_MT_POSITION_X, 0, 1023);
    abs_setup(ABS_MT_POSITION_Y, 0, 1023);

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    strcpy(setup.name, "kenzo-test-touch");
    ioctl(fd, UI_DEV_SETUP, &setup);
    ioctl(fd, UI_DEV_CREATE);
}

static void run_line(const char *line)
{
    int s, id, x, y, n, ms, dx, dy;
    if (sscanf(line, "d %d %d %d %d", &s, &id, &x, &y) == 4) {
        emit(EV_ABS, ABS_MT_SLOT, s);
        emit(EV_ABS, ABS_MT_TRACKING_ID, id);
        move(s, x, y);
        if (active++ == 0) {
            emit(EV_KEY, BTN_TOUCH, 1);
        }
    } else if (sscanf(line, "m %d %d %d", &s, &x, &y) == 3) {
        move(s, x, y);
    } else if (sscanf(line, "g2 %d %d %d %d", &dx, &dy, &n, &ms) == 4) {
        int x0 = pos_x[0], y0 = pos_y[0], x1 = pos_x[1], y1 = pos_y[1];
        for (int i = 1; i <= n; i++) {
            move(0, x0 + dx * i / n, y0 + dy * i / n);
            move(1, x1 + dx * i / n, y1 + dy * i / n);
            frame();
            usleep(ms * 1000);
        }
    } else if (sscanf(line, "g %d %d %d %d %d", &s, &x, &y, &n, &ms) == 5) {
        int x0 = pos_x[s], y0 = pos_y[s];
        for (int i = 1; i <= n; i++) {
            move(s, x0 + (x - x0) * i / n, y0 + (y - y0) * i / n);
            frame();
            usleep(ms * 1000);
        }
    } else if (sscanf(line, "u %d", &s) == 1) {
        emit(EV_ABS, ABS_MT_SLOT, s);
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        if (--active == 0) {
            emit(EV_KEY, BTN_TOUCH, 0);
        }
    } else if (sscanf(line, "f %d", &n) == 1) {
        int x0 = pos_x[0], y0 = pos_y[0];
        for (int i = 0; i < n; i++) {
            move(0, x0 + i % 2, y0 + i % 3);
            frame();
        }
    } else if (sscanf(line, "w %d", &ms) == 1) {
        usleep(ms * 1000);
    } else if (line[0] == 's') {
        frame();
    } else if (line[0] == 'x' && line[1] == ' ') {
        printf("> %s", line + 2);
        fflush(stdout);
        if (system(line + 2) != 0) {
            printf("  (команда вернула ошибку)\n");
        }
        fflush(stdout);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "использование: vtouch сценарий\n");
        return 2;
    }
    FILE *script = fopen(argv[1], "r");
    if (script == NULL) {
        perror(argv[1]);
        return 1;
    }
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("/dev/uinput");
        return 1;
    }
    create_device();
    sleep(3);

    char line[512];
    while (fgets(line, sizeof(line), script)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        run_line(line);
    }
    sleep(1);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    fclose(script);
    return 0;
}
