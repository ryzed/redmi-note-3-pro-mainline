# Сборка kenzo-power на телефоне: make
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LIBS = -levdev -lXtst -lX11 -lXext

kenzo-power: kenzo-power.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f kenzo-power

.PHONY: clean
