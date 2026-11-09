# Сборка kenzo-touch на телефоне: make
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LIBS = -levdev -lXtst -lX11 -lXext -lXrandr

kenzo-touch: kenzo-touch.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f kenzo-touch

.PHONY: clean
