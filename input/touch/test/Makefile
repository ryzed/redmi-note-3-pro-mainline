# Виртуальный тачскрин для проверки kenzo-touch: make, затем ./run.sh сценарий
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

vtouch: vtouch.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f vtouch vtouch.out kt-test.log overlay.png picked.png

.PHONY: clean
