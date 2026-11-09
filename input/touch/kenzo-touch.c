/*
 * kenzo-touch — тачскрин Redmi Note 3 как обычная мышь.
 *
 * Демон забирает тачскрин себе (EVIOCGRAB через libevdev), поэтому X его
 * больше не видит: касание не превращается в лишний левый клик. Вместо этого
 * демон сам шлёт мышиные события через XTest — так же, как svkbd шлёт клавиши.
 *
 * Жесты:
 *   тап                   — левый клик;
 *   сдвиг                 — перетаскивание (кнопка зажата, пока палец на экране);
 *   удержание без движения — правый клик (контекстное меню);
 *   два пальца вверх/вниз — прокрутка колесом.
 *
 * Размер и поворот экрана берём у X (RandR) и следим за их сменой: кнопка «⟳»
 * в панели jwm переключает портрет и альбом, а оси тачскрина при этом остаются
 * физическими. Поворот посреди жеста сбрасывает жест: отпускаем кнопку и ждём
 * отрыва пальцев.
 *
 * Первое касание погашенного экрана только зажигает его: иначе тап заодно
 * нажал бы кнопку в окне под пальцем.
 *
 * На экранной клавиатуре svkbd жестов нет: левая кнопка зажата, пока палец на
 * экране. svkbd сам считает долгое нажатие (ё и ъ, повтор Backspace), а
 * правый клик по клавише он понимает как Alt+клавиша.
 *
 * Сборка: make   (нужны gcc, musl-dev, libevdev-dev, libxtst-dev, libx11-dev,
 *                 libxext-dev, libxrandr-dev)
 * Запуск: kenzo-touch [-d устройство] [-v]
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
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/Xrandr.h>

/*
 * Постоянное имя тачскрина из правила udev 99-kenzo-touch.rules: номер eventN
 * зависит от порядка, в котором ядро нашло устройства, и может съехать.
 */
#define DEVICE "/dev/input/kenzo-touch"
/* Размер экрана до первого ответа RandR (портрет). */
#define SCREEN_W 1080
#define SCREEN_H 1920

/* Класс окна экранной клавиатуры (WM_CLASS). */
#define KEYBOARD_CLASS "svkbd"

/*
 * Состояние панели. Берём из sysfs, а не у X: xset q и после пробуждения
 * продолжает говорить «Monitor is Off» (проверено 05.11), а атрибут
 * панели меняется вместе с подсветкой.
 */
#define PANEL_DPMS "/sys/class/drm/card0-DSI-1/dpms"

/*
 * Пороги — в пикселях экрана (логического, после поворота). Тачскрин отдаёт
 * 0..1023 по обеим осям при экране 1080×1920, поэтому сравнивать сырые единицы
 * нельзя: по вертикали порог вышел бы почти вдвое крупнее.
 */
#define HOLD_MS 600      /* столько держать палец без движения — правый клик */
#define MOVE_PX 20       /* сдвиг больше этого — уже перетаскивание */
#define SCROLL_STEP 75   /* столько пикселей на один щелчок колеса */
#define CLICK_MS 60      /* пауза между нажатием и отпусканием в клике */
#define MAX_SLOTS 10

/* Состояние жеста. */
enum mode {
    MODE_IDLE,      /* пальцев нет */
    MODE_PENDING,   /* палец опущен, ещё не ясно: тап, сдвиг или удержание */
    MODE_DRAG,      /* перетаскивание: левая кнопка зажата */
    MODE_KEYBOARD,  /* палец на экранной клавиатуре: левая кнопка зажата */
    MODE_WAIT,      /* ждём отрыва всех пальцев: правый клик уже сделан
                       или события потеряны и жест не восстановить */
    MODE_SCROLL,    /* прокрутка двумя пальцами */
    MODE_WAKE,      /* касание погашенного экрана: только разбудить */
};

struct finger {
    int tracking;   /* ABS_MT_TRACKING_ID; -1 — слот свободен */
    int x;
    int y;
};

struct state {
    enum mode mode;
    struct finger slots[MAX_SLOTS];
    int slot;               /* текущий слот из ABS_MT_SLOT */
    int last_count;         /* сколько пальцев было в прошлом кадре */
    int down_x;             /* точка касания, сырые единицы тачскрина */
    int down_y;
    long long down_ms;      /* когда палец опустился */
    int last_avg_x;         /* средняя точка пальцев, пиксели экрана */
    int last_avg_y;
    int scroll_x;           /* накопленное движение для колеса, пиксели */
    int scroll_y;
};

static volatile sig_atomic_t stopping = 0;
static int verbose = 0;

struct screen {
    Display *display;
    int width;              /* размер экрана в X, с учётом поворота */
    int height;
    Rotation rotation;      /* RR_Rotate_0/90/180/270 */
    int rr_event_base;      /* номер первого события RandR */
    int min_x, max_x;
    int min_y, max_y;
    int dpms;               /* есть ли в X расширение DPMS */
};

/* Отметка действия в журнале: видно, что демон распознал. */
static void say(const char *what, int x, int y)
{
    if (verbose) {
        fprintf(stderr, "%s (%d,%d)\n", what, x, y);
    }
}

/* Размер и поворот экрана в журнале: видно, как демон понимает оси. */
static void say_screen(const char *what, const struct screen *screen)
{
    if (verbose) {
        int degrees = screen->rotation == RR_Rotate_90 ? 90
                    : screen->rotation == RR_Rotate_180 ? 180
                    : screen->rotation == RR_Rotate_270 ? 270 : 0;
        fprintf(stderr, "%s %dx%d, поворот %d\n", what, screen->width,
                screen->height, degrees);
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

static int active_count(const struct state *state)
{
    int count = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (state->slots[i].tracking != -1) {
            count++;
        }
    }
    return count;
}

/* Средняя точка всех активных пальцев: по ней считаем прокрутку. */
static void active_average(const struct state *state, int *x, int *y, int *count)
{
    long sum_x = 0;
    long sum_y = 0;
    int n = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (state->slots[i].tracking != -1) {
            sum_x += state->slots[i].x;
            sum_y += state->slots[i].y;
            n++;
        }
    }
    *count = n;
    *x = n ? (int)(sum_x / n) : 0;
    *y = n ? (int)(sum_y / n) : 0;
}

/* Первый активный палец: пока он один, это и есть точка касания. */
static int active_point(const struct state *state, int *x, int *y)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (state->slots[i].tracking != -1) {
            *x = state->slots[i].x;
            *y = state->slots[i].y;
            return 1;
        }
    }
    return 0;
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

/* Одна ось: сырое значение (min..max) в 0..extent-1; flip — зеркально. */
static int scale_axis(int raw, int min, int max, int extent, int flip)
{
    if (max <= min) {
        return 0;
    }
    long value = (long)(raw - min) * (extent - 1) / (max - min);
    return flip ? (int)(extent - 1 - value) : (int)value;
}

/*
 * Сырая точка тачскрина (0..1023 по осям) в пиксели экрана с учётом поворота.
 * Оси тачскрина физические: при повороте на 90° они меняются местами, поэтому
 * x и y считаются вместе, а не по отдельности. Ширина и высота экрана — уже
 * после поворота. RR_Rotate_270 — это «xrandr --rotate right».
 */
static void map_point(const struct screen *screen, int raw_x, int raw_y,
                      int *x, int *y)
{
    int w = screen->width;
    int h = screen->height;
    switch (screen->rotation) {
    case RR_Rotate_270:
        *x = scale_axis(raw_y, screen->min_y, screen->max_y, w, 0);
        *y = scale_axis(raw_x, screen->min_x, screen->max_x, h, 1);
        break;
    case RR_Rotate_90:
        *x = scale_axis(raw_y, screen->min_y, screen->max_y, w, 1);
        *y = scale_axis(raw_x, screen->min_x, screen->max_x, h, 0);
        break;
    case RR_Rotate_180:
        *x = scale_axis(raw_x, screen->min_x, screen->max_x, w, 1);
        *y = scale_axis(raw_y, screen->min_y, screen->max_y, h, 1);
        break;
    default:
        *x = scale_axis(raw_x, screen->min_x, screen->max_x, w, 0);
        *y = scale_axis(raw_y, screen->min_y, screen->max_y, h, 0);
        break;
    }
}

static void mouse_move(struct screen *screen, int x, int y)
{
    XTestFakeMotionEvent(screen->display, -1, x, y, CurrentTime);
    XFlush(screen->display);
}

static void mouse_button(struct screen *screen, int button, int press)
{
    XTestFakeButtonEvent(screen->display, button, press ? True : False,
                         CurrentTime);
    XFlush(screen->display);
}

/*
 * Клик: нажатие, пауза, отпускание. Без паузы кнопки панели jwm («⌨», «⟳»)
 * не срабатывают: jwm на нажатие захватывает мышь и ждёт отпускания, а
 * отпускание, пришедшее раньше захвата, теряет (проверено 06.11 xdotool: без
 * паузы — 0 из 2, с паузой от 10 мс — 6 из 6). Живой палец на тачпаде всегда
 * держит кнопку дольше, поэтому там этого не видно.
 */
static void mouse_click(struct screen *screen, int button)
{
    mouse_button(screen, button, 1);
    usleep(CLICK_MS * 1000);
    mouse_button(screen, button, 0);
}

/* Один щелчок колеса: нажатие и отпускание одной кнопки. */
static void wheel_click(struct screen *screen, int button)
{
    mouse_button(screen, button, 1);
    mouse_button(screen, button, 0);
}

/* Отпустить левую кнопку, если жест держит её зажатой. */
static void release_left(struct screen *screen, struct state *state)
{
    if (state->mode == MODE_DRAG || state->mode == MODE_KEYBOARD) {
        mouse_button(screen, 1, 0);
    }
}

/*
 * Окно под точкой — экранная клавиатура? Спускаемся от корня по дочерним
 * окнам и смотрим класс на каждом уровне: оконный менеджер может обернуть
 * окно своей рамкой.
 */
static int over_keyboard(struct screen *screen, int x, int y)
{
    Window root = DefaultRootWindow(screen->display);
    Window window = root;
    Window child = None;
    int child_x = 0;
    int child_y = 0;
    while (XTranslateCoordinates(screen->display, root, window, x, y,
                                 &child_x, &child_y, &child)
           && child != None) {
        window = child;
        XClassHint hint;
        if (XGetClassHint(screen->display, window, &hint)) {
            int found = hint.res_class != NULL
                        && strcmp(hint.res_class, KEYBOARD_CLASS) == 0;
            if (hint.res_name != NULL) {
                XFree(hint.res_name);
            }
            if (hint.res_class != NULL) {
                XFree(hint.res_class);
            }
            if (found) {
                return 1;
            }
        }
    }
    return 0;
}

/* Средняя точка активных пальцев в пикселях экрана. */
static void average_px(const struct screen *screen, const struct state *state,
                       int *x, int *y, int *count)
{
    int raw_x = 0;
    int raw_y = 0;
    active_average(state, &raw_x, &raw_y, count);
    map_point(screen, raw_x, raw_y, x, y);
}

/* Начать прокрутку: отпустить левую кнопку, если она была зажата. */
static void enter_scroll(struct screen *screen, struct state *state)
{
    int count = 0;
    release_left(screen, state);
    state->mode = MODE_SCROLL;
    average_px(screen, state, &state->last_avg_x, &state->last_avg_y, &count);
    state->scroll_x = 0;
    state->scroll_y = 0;
}

/*
 * Прокрутка: движение средней точки в щелчки колеса. Если палец добавился
 * или ушёл, средняя точка прыгает — тогда только запоминаем её заново.
 */
static void scroll_frame(struct screen *screen, struct state *state,
                         int fingers_changed)
{
    int avg_x = 0;
    int avg_y = 0;
    int count = 0;
    average_px(screen, state, &avg_x, &avg_y, &count);
    if (count < 2) {
        return;
    }
    if (fingers_changed) {
        state->last_avg_x = avg_x;
        state->last_avg_y = avg_y;
        return;
    }
    state->scroll_x += avg_x - state->last_avg_x;
    state->scroll_y += avg_y - state->last_avg_y;
    state->last_avg_x = avg_x;
    state->last_avg_y = avg_y;

    /*
     * Как на телефоне: содержимое едет за пальцами. Пальцы вверх — колесо
     * вниз (текст уходит вверх, видно дальше), пальцы вправо — колесо влево.
     * До 09.11 было как у колеса мыши — пользователю неинтуитивно.
     */
    while (state->scroll_y <= -SCROLL_STEP) {
        wheel_click(screen, 5);     /* колесо вниз */
        say("прокрутка вниз", avg_x, avg_y);
        state->scroll_y += SCROLL_STEP;
    }
    while (state->scroll_y >= SCROLL_STEP) {
        wheel_click(screen, 4);     /* колесо вверх */
        say("прокрутка вверх", avg_x, avg_y);
        state->scroll_y -= SCROLL_STEP;
    }
    while (state->scroll_x <= -SCROLL_STEP) {
        wheel_click(screen, 7);     /* колесо вправо */
        say("прокрутка вправо", avg_x, avg_y);
        state->scroll_x += SCROLL_STEP;
    }
    while (state->scroll_x >= SCROLL_STEP) {
        wheel_click(screen, 6);     /* колесо влево */
        say("прокрутка влево", avg_x, avg_y);
        state->scroll_x -= SCROLL_STEP;
    }
}

/*
 * Конец кадра: libevdev отдаёт события пачками до SYN_REPORT, поэтому все
 * решения принимаются здесь, когда координаты уже известны.
 */
static void process_frame(struct screen *screen, struct state *state)
{
    int count = active_count(state);
    int fingers_changed = count != state->last_count;
    state->last_count = count;

    if (count == 0) {
        if (state->mode == MODE_PENDING) {
            /* Палец ушёл раньше удержания и не сдвинулся — это тап. */
            int px = 0;
            int py = 0;
            map_point(screen, state->down_x, state->down_y, &px, &py);
            mouse_move(screen, px, py);
            mouse_click(screen, 1);
            say("тап", px, py);
        } else if (state->mode == MODE_DRAG) {
            mouse_button(screen, 1, 0);
            say("перетаскивание закончено", 0, 0);
        } else if (state->mode == MODE_KEYBOARD) {
            mouse_button(screen, 1, 0);
            say("клавиатура: отпущено", 0, 0);
        } else if (state->mode == MODE_WAKE) {
            say("пробуждение", 0, 0);
        }
        state->mode = MODE_IDLE;
        return;
    }

    if (count >= 2) {
        if (state->mode == MODE_WAKE || state->mode == MODE_KEYBOARD) {
            /*
             * Будили экран или нажимаем клавишу — остальные пальцы ничего
             * не значат.
             */
            return;
        }
        if (state->mode != MODE_SCROLL) {
            enter_scroll(screen, state);
            fingers_changed = 1;
        }
        scroll_frame(screen, state, fingers_changed);
        return;
    }

    /* Один палец. */
    int x = 0;
    int y = 0;
    if (!active_point(state, &x, &y)) {
        return;
    }

    switch (state->mode) {
    case MODE_IDLE:
        if (screen_is_off()) {
            /*
             * Экран погашен: касание только зажигает его. Иначе тап
             * заодно нажал бы кнопку в окне под пальцем.
             */
            state->mode = MODE_WAKE;
            screen_set(screen, 1);
            break;
        }
        int cx = 0;
        int cy = 0;
        map_point(screen, x, y, &cx, &cy);
        if (over_keyboard(screen, cx, cy)) {
            /*
             * Экранная клавиатура: кнопка зажата сразу и до отрыва пальца,
             * долгое нажатие svkbd считает сам.
             */
            state->mode = MODE_KEYBOARD;
            mouse_move(screen, cx, cy);
            mouse_button(screen, 1, 1);
            say("клавиатура: нажато", cx, cy);
            break;
        }
        state->mode = MODE_PENDING;
        state->down_x = x;
        state->down_y = y;
        state->down_ms = now_ms();
        break;

    case MODE_PENDING: {
        int down_px = 0;
        int down_py = 0;
        int px = 0;
        int py = 0;
        map_point(screen, state->down_x, state->down_y, &down_px, &down_py);
        map_point(screen, x, y, &px, &py);
        if (abs(px - down_px) > MOVE_PX || abs(py - down_py) > MOVE_PX) {
            /*
             * Сдвинулся — значит, тащим. Кнопку жмём в точке касания, а не
             * там, где палец ушёл за порог: иначе выделение текста или
             * ползунок начинались бы со смещением.
             */
            state->mode = MODE_DRAG;
            mouse_move(screen, down_px, down_py);
            mouse_button(screen, 1, 1);
            mouse_move(screen, px, py);
            say("перетаскивание", down_px, down_py);
        }
        break;
    }

    case MODE_DRAG:
    case MODE_KEYBOARD: {
        int px = 0;
        int py = 0;
        map_point(screen, x, y, &px, &py);
        mouse_move(screen, px, py);
        break;
    }

    case MODE_WAIT:
    case MODE_SCROLL:
    case MODE_WAKE:
        /* После правого клика, прокрутки или пробуждения ждём отрыва пальца. */
        break;
    }
}

/* Удержание истекло, палец на месте — правый клик. */
static void check_hold(struct screen *screen, struct state *state)
{
    if (state->mode != MODE_PENDING) {
        return;
    }
    if (now_ms() - state->down_ms < HOLD_MS) {
        return;
    }
    int x = 0;
    int y = 0;
    if (!active_point(state, &x, &y)) {
        return;
    }
    int px = 0;
    int py = 0;
    map_point(screen, x, y, &px, &py);
    mouse_move(screen, px, py);
    mouse_click(screen, 3);
    say("правый клик", px, py);
    state->mode = MODE_WAIT;
}

/* Начальное состояние: пальцев нет. */
static void init_state(struct state *state)
{
    memset(state, 0, sizeof(*state));
    for (int i = 0; i < MAX_SLOTS; i++) {
        state->slots[i].tracking = -1;
    }
    state->mode = MODE_IDLE;
}

/* Прочитать у X размер экрана и поворот. */
static void screen_refresh(struct screen *screen)
{
    Window root = DefaultRootWindow(screen->display);
    XRRScreenConfiguration *config = XRRGetScreenInfo(screen->display, root);
    if (config != NULL) {
        Rotation current = RR_Rotate_0;
        XRRConfigRotations(config, &current);   /* возвращает допустимые, текущий — через аргумент */
        screen->rotation = current & (RR_Rotate_0 | RR_Rotate_90
                                      | RR_Rotate_180 | RR_Rotate_270);
        XRRFreeScreenConfigInfo(config);
    }
    int number = DefaultScreen(screen->display);
    screen->width = DisplayWidth(screen->display, number);
    screen->height = DisplayHeight(screen->display, number);
}

/*
 * Экран повернули или сменили размер. Начатый жест считан в старых
 * координатах и пересчёту не поддаётся: отпускаем кнопку и ждём отрыва пальцев.
 */
static void screen_changed(struct screen *screen, struct state *state)
{
    screen_refresh(screen);
    say_screen("экран", screen);
    if (state->mode != MODE_IDLE) {
        release_left(screen, state);
        state->mode = active_count(state) > 0 ? MODE_WAIT : MODE_IDLE;
    }
}

/* Разобрать события X: нас интересует только смена экрана RandR. */
static void handle_x_events(struct screen *screen, struct state *state)
{
    while (XPending(screen->display) > 0) {
        XEvent event;
        XNextEvent(screen->display, &event);
        if (event.type == screen->rr_event_base + RRScreenChangeNotify) {
            XRRUpdateConfiguration(&event);
            screen_changed(screen, state);
        }
    }
}

static void handle_event(struct state *state, const struct input_event *event)
{
    switch (event->type) {
    case EV_ABS:
        if (event->code == ABS_MT_SLOT) {
            if (event->value >= 0 && event->value < MAX_SLOTS) {
                state->slot = event->value;
            }
        } else if (event->code == ABS_MT_TRACKING_ID) {
            /*
             * Координаты тачскрин присылает только при изменении, поэтому
             * при новом касании в ту же точку позиция слота остаётся от
             * прошлого раза — и это правильная точка тапа. Обнулять нельзя:
             * тап кликнул бы в левый верхний угол.
             */
            state->slots[state->slot].tracking = event->value;
        } else if (event->code == ABS_MT_POSITION_X) {
            state->slots[state->slot].x = event->value;
        } else if (event->code == ABS_MT_POSITION_Y) {
            state->slots[state->slot].y = event->value;
        }
        break;
    default:
        break;
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
    /* Без координат касаний это не тачскрин: чужое устройство не забираем. */
    if (!libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X)
        || !libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y)) {
        fprintf(stderr, "%s — не тачскрин (%s)\n", device,
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
    screen.width = SCREEN_W;
    screen.height = SCREEN_H;
    screen.rotation = RR_Rotate_0;
    const struct input_absinfo *abs_x =
        libevdev_get_abs_info(evdev, ABS_MT_POSITION_X);
    const struct input_absinfo *abs_y =
        libevdev_get_abs_info(evdev, ABS_MT_POSITION_Y);
    screen.min_x = abs_x ? abs_x->minimum : 0;
    screen.max_x = abs_x ? abs_x->maximum : 1023;
    screen.min_y = abs_y ? abs_y->minimum : 0;
    screen.max_y = abs_y ? abs_y->maximum : 1023;

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

    int rr_error_base = 0;
    if (!XRRQueryExtension(screen.display, &screen.rr_event_base,
                           &rr_error_base)) {
        fprintf(stderr, "в X нет расширения RandR\n");
        XCloseDisplay(screen.display);
        libevdev_grab(evdev, LIBEVDEV_UNGRAB);
        libevdev_free(evdev);
        close(fd);
        return 1;
    }
    XRRSelectInput(screen.display, DefaultRootWindow(screen.display),
                   RRScreenChangeNotifyMask);
    screen_refresh(&screen);
    say_screen("экран", &screen);
    int x_fd = ConnectionNumber(screen.display);

    struct state state;
    init_state(&state);

    while (!stopping) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        FD_SET(x_fd, &set);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50 * 1000;

        /* Сначала события X, прочитанные Xlib заранее: select о них не знает. */
        handle_x_events(&screen, &state);
        int ready = select((fd > x_fd ? fd : x_fd) + 1, &set, NULL, NULL,
                           &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "select: %s\n", strerror(errno));
            break;
        }
        if (ready == 0 || !FD_ISSET(fd, &set)) {
            handle_x_events(&screen, &state);
            check_hold(&screen, &state);
            continue;
        }
        handle_x_events(&screen, &state);

        struct input_event event;
        int rc;
        while ((rc = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL,
                                         &event)) == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                process_frame(&screen, &state);
                continue;
            }
            handle_event(&state, &event);
        }
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            /*
             * Ядро потеряло события (SYN_DROPPED приходит этим кодом, а не
             * как обычное событие). libevdev отдаёт текущее состояние
             * пальцев, но начатый жест уже не восстановить: отпускаем кнопку
             * и ждём, пока все пальцы уйдут.
             *
             * Каждое событие этой порции тоже приходит кодом SYNC, а не
             * SUCCESS; конец порции — -EAGAIN. Цикл по SUCCESS выходил сразу,
             * палец со старого жеста оставался «на экране», и первый новый
             * тап уходил на ожидание его отрыва (найдено стендом 06.11).
             */
            release_left(&screen, &state);
            while (libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_SYNC,
                                       &event) == LIBEVDEV_READ_STATUS_SYNC) {
                handle_event(&state, &event);
            }
            state.last_count = active_count(&state);
            state.mode = state.last_count > 0 ? MODE_WAIT : MODE_IDLE;
            say("события потеряны, жест сброшен", 0, 0);
        }
        check_hold(&screen, &state);
    }

    release_left(&screen, &state);
    if (verbose) {
        fprintf(stderr, "выхожу\n");
    }
    XCloseDisplay(screen.display);
    libevdev_grab(evdev, LIBEVDEV_UNGRAB);
    libevdev_free(evdev);
    close(fd);
    return 0;
}
