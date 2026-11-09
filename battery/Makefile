# kenzo-battery: сборка на телефоне — apk add gcc musl-dev libx11-dev libxft-dev make; make; sudo make install
CFLAGS ?= -O2 -Wall -Wextra
XFT_CFLAGS := $(shell pkg-config --cflags xft)
XFT_LIBS := $(shell pkg-config --libs xft)

kenzo-battery: kenzo-battery.c
	$(CC) $(CFLAGS) $(XFT_CFLAGS) -o $@ $< $(XFT_LIBS) -lX11

install: kenzo-battery
	install -m 755 kenzo-battery /usr/local/bin/kenzo-battery

clean:
	rm -f kenzo-battery

.PHONY: install clean
