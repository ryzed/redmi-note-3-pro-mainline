/*
 * kenzo-battery: заряд батареи текстом в панели jwm (Redmi Note 3 «kenzo»).
 *
 * Окно 110×96 цвета панели с надписью «82%»; при зарядке — «⚡82%», ниже 15 % —
 * красным. Данные — /sys/class/power_supply/qcom-battery (capacity, status),
 * раз в 30 с. Вставляется в <Tray> файла ~/.jwmrc:
 *   <Swallow name="kenzo-battery" width="110">kenzo-battery</Swallow>
 *
 * Своя программа вместо готовых (09.11): значок cbatticon (GTK3) в трее jwm не
 * рисовался, а conky рисует с двойной буферизацией (DBE), и при modesetting +
 * glamor его перерисовки до экрана не доходили — текст был виден только на
 * снимке scrot. Здесь текст рисуется прямо в окно через Xft, как у самой jwm.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

#define BATTERY "/sys/class/power_supply/qcom-battery"
#define FONT "DejaVu Sans:pixelsize=27"   /* как Clock в ~/.jwmrc (DejaVu Sans-20) */
#define COLOR_BG "#202830"                /* TrayStyle Background */
#define COLOR_FG "#FFFFFF"                /* TrayStyle Foreground */
#define COLOR_LOW "#FF5555"
#define LOW_LEVEL 15
#define PERIOD_S 30
#define WIDTH 110
#define HEIGHT 96

struct indicator {
    Display *dpy;
    Window win;
    Visual *visual;
    Colormap colormap;
    int depth;
    Pixmap pixmap;          /* картинка надписи — фон окна */
    int pixmap_width;
    int pixmap_height;
    XftDraw *draw;          /* рисует в pixmap, не в окно */
    XftFont *font;
    XftColor bg;
    XftColor fg;
    XftColor low;
    int width;
    int height;
    char text[32];
    int is_low;
    int swallowed;          /* jwm вставила окно в панель */
};

/* Первая строка файла без перевода строки; 0 — прочитано. */
static int read_line(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    if (!fgets(buf, (int)size, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

static void read_battery(struct indicator *ind)
{
    char capacity[16];
    char status[32];
    int level;

    if (read_line(BATTERY "/capacity", capacity, sizeof capacity) != 0
            || sscanf(capacity, "%d", &level) != 1) {
        snprintf(ind->text, sizeof ind->text, "?%%");
        ind->is_low = 0;
        return;
    }
    if (read_line(BATTERY "/status", status, sizeof status) != 0)
        status[0] = '\0';
    snprintf(ind->text, sizeof ind->text, "%s%d%%",
             strcmp(status, "Charging") == 0 ? "⚡" : "", level);
    ind->is_low = level < LOW_LEVEL;
}

/*
 * jwm запускает команду <Swallow> при старте сеанса, и окно, показанное раньше,
 * чем jwm готова его забрать, становится обычным окном (09.11: после загрузки
 * индикатор оказался внизу экрана, а в панели — пустое место). Ждём, пока
 * оконный менеджер объявит себя на корневом окне, и ещё полсекунды.
 */
static void wait_for_wm(Display *dpy)
{
    Atom check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    struct timespec step = { 0, 100 * 1000 * 1000 };
    struct timespec settle = { 0, 500 * 1000 * 1000 };
    int i;

    for (i = 0; i < 150; i++) {
        Atom type;
        int format;
        unsigned long count;
        unsigned long rest;
        unsigned char *data = NULL;
        int found = XGetWindowProperty(dpy, DefaultRootWindow(dpy), check, 0, 1, False,
                                       AnyPropertyType, &type, &format, &count, &rest,
                                       &data) == Success && type != None && count > 0;
        if (data)
            XFree(data);
        if (found)
            break;
        nanosleep(&step, NULL);
    }
    nanosleep(&settle, NULL);
}

/*
 * Надпись рисуется в pixmap, а pixmap становится фоном окна: на экран её выводит
 * сам X простым копированием. 09.11 текст, нарисованный Xft прямо в окно, на
 * экране телефона то был, то пропадал и выходил кривыми символами, а на снимке
 * scrot всё было в порядке.
 */
static void paint(struct indicator *ind)
{
    XGlyphInfo ext;
    int len = (int)strlen(ind->text);
    int x;
    int y;

    if (!ind->pixmap || ind->pixmap_width != ind->width
            || ind->pixmap_height != ind->height) {
        if (ind->draw)
            XftDrawDestroy(ind->draw);
        if (ind->pixmap)
            XFreePixmap(ind->dpy, ind->pixmap);
        ind->pixmap = XCreatePixmap(ind->dpy, ind->win, (unsigned)ind->width,
                                    (unsigned)ind->height, (unsigned)ind->depth);
        ind->pixmap_width = ind->width;
        ind->pixmap_height = ind->height;
        ind->draw = XftDrawCreate(ind->dpy, ind->pixmap, ind->visual, ind->colormap);
    }

    XftDrawRect(ind->draw, &ind->bg, 0, 0, (unsigned)ind->width, (unsigned)ind->height);
    XftTextExtentsUtf8(ind->dpy, ind->font, (const FcChar8 *)ind->text, len, &ext);
    x = (ind->width - ext.xOff) / 2;
    y = (ind->height + ind->font->ascent - ind->font->descent) / 2;
    XftDrawStringUtf8(ind->draw, ind->is_low ? &ind->low : &ind->fg, ind->font,
                      x, y, (const FcChar8 *)ind->text, len);

    XSetWindowBackgroundPixmap(ind->dpy, ind->win, ind->pixmap);
    XClearWindow(ind->dpy, ind->win);
    XFlush(ind->dpy);
}

int main(void)
{
    struct indicator ind = { 0 };
    XClassHint class_hint = { "kenzo-battery", "kenzo-battery" };
    int screen;
    Window root;
    int x_fd;
    time_t next_update;

    ind.dpy = XOpenDisplay(NULL);
    if (!ind.dpy) {
        fprintf(stderr, "kenzo-battery: нет дисплея X (DISPLAY не задан?)\n");
        return 1;
    }
    screen = DefaultScreen(ind.dpy);
    root = RootWindow(ind.dpy, screen);
    ind.visual = DefaultVisual(ind.dpy, screen);
    ind.colormap = DefaultColormap(ind.dpy, screen);
    ind.depth = DefaultDepth(ind.dpy, screen);

    ind.font = XftFontOpenName(ind.dpy, screen, FONT);
    if (!ind.font
            || !XftColorAllocName(ind.dpy, ind.visual, ind.colormap, COLOR_BG, &ind.bg)
            || !XftColorAllocName(ind.dpy, ind.visual, ind.colormap, COLOR_FG, &ind.fg)
            || !XftColorAllocName(ind.dpy, ind.visual, ind.colormap, COLOR_LOW, &ind.low)) {
        fprintf(stderr, "kenzo-battery: не открыть шрифт или цвета\n");
        return 1;
    }

    ind.width = WIDTH;
    ind.height = HEIGHT;
    ind.win = XCreateSimpleWindow(ind.dpy, root, 0, 0, WIDTH, HEIGHT, 0,
                                  ind.bg.pixel, ind.bg.pixel);
    XStoreName(ind.dpy, ind.win, "kenzo-battery");
    XSetClassHint(ind.dpy, ind.win, &class_hint);
    XSelectInput(ind.dpy, ind.win, ExposureMask | StructureNotifyMask);
    read_battery(&ind);
    paint(&ind);
    wait_for_wm(ind.dpy);
    XMapWindow(ind.dpy, ind.win);

    x_fd = ConnectionNumber(ind.dpy);
    next_update = time(NULL) + PERIOD_S;

    for (;;) {
        struct timeval timeout = { 0, 0 };
        time_t now;
        fd_set fds;

        while (XPending(ind.dpy)) {
            XEvent ev;
            XNextEvent(ind.dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0) {
                paint(&ind);
            } else if (ev.type == ConfigureNotify) {
                if (ev.xconfigure.width != ind.width || ev.xconfigure.height != ind.height) {
                    ind.width = ev.xconfigure.width;
                    ind.height = ev.xconfigure.height;
                    paint(&ind);
                }
            } else if (ev.type == ReparentNotify) {
                /*
                 * Вставили в панель — запомнить. Вернули на корневое окно —
                 * jwm перезапускается (kenzo-osk-toggle делает jwm -restart) и
                 * сама запустит новый экземпляр для панели; этот, оставшись,
                 * стал бы обычным окном внизу экрана.
                 */
                if (ev.xreparent.parent != root)
                    ind.swallowed = 1;
                else if (ind.swallowed)
                    return 0;
            } else if (ev.type == DestroyNotify) {
                return 0;
            }
        }

        /* срок обновления фиксированный: частые события X его не отодвигают */
        now = time(NULL);
        if (now >= next_update) {
            read_battery(&ind);
            paint(&ind);
            next_update = now + PERIOD_S;
        }
        timeout.tv_sec = next_update - now;

        FD_ZERO(&fds);
        FD_SET(x_fd, &fds);
        if (select(x_fd + 1, &fds, NULL, NULL, &timeout) < 0 && errno != EINTR) {
            perror("kenzo-battery: select");
            return 1;
        }
    }
}
