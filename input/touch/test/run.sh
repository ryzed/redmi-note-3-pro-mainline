#!/bin/sh
# Прогон сценария на телефоне: виртуальный тачскрин (vtouch) и тестовая копия
# kenzo-touch на нём. Настоящий демон не трогается: копия называется kt-test,
# поэтому сценарий может останавливать её (pkill -x kt-test), не задевая его.
#
# Касания приходят в настоящий X — снять экран до прогона и знать, что под
# пальцем. Сценарии рассчитаны на развёрнутый xterm в фокусе и svkbd на слое en.
#
#   make                   (gcc есть на телефоне)
#   ./run.sh keyboard.txt [путь к kenzo-touch]
#
# Снимки экрана из сценария (scrot) ложатся сюда же; make clean их убирает.
#
# ROTATE=right ./run.sh landscape.txt — повернуть экран перед прогоном (через
# kenzo-rotate, он же перекладывает svkbd) и вернуть портрет после. На время
# прогона ставится автооткат: таймер kenzo-rotate-revert, 4 мин — оборвётся
# прогон, экран вернётся сам.
#
# Итог — журнал жестов демона (-v) и вывод команд «x» из сценария; что должно
# получиться, написано в комментариях сценария.
set -e
rotate_user=${ROTATE:-}
scenario=$(realpath "$1")
binary=$(realpath "${2:-/usr/local/bin/kenzo-touch}")
cd "$(dirname "$0")"

# Поворот: kenzo-rotate запускаем отдельной службой с KillMode=none, иначе
# svkbd, поднятая внутри него, умерла бы вместе со службой (и с сеансом ssh).
rotate_to() {
    DISPLAY=:0 xrandr --query | grep -q "^DSI-1 .* right (" && now=right || now=normal
    [ "$now" = "$1" ] && return 0
    systemd-run --user --quiet --wait --collect -p KillMode=none \
        -E DISPLAY=:0 /usr/local/bin/kenzo-rotate
    sleep 1
}
if [ -n "$rotate_user" ]; then
    systemd-run --user --quiet --on-active=240 --unit=kenzo-rotate-revert \
        -E DISPLAY=:0 /usr/bin/xrandr --output DSI-1 --rotate normal
    rotate_to "$rotate_user"
fi

sudo -n ./vtouch "$scenario" > vtouch.out 2>&1 &
vtouch=$!
sleep 1
device=
for e in /sys/class/input/event*; do
    [ "$(cat "$e/device/name")" = kenzo-test-touch ] && device=/dev/input/$(basename "$e")
done
[ -n "$device" ] || { echo "виртуальный тачскрин не появился"; exit 1; }
sudo -n chmod 666 "$device"

cp "$binary" /tmp/kt-test
DISPLAY=:0 /tmp/kt-test -v -d "$device" > kt-test.log 2>&1 &
daemon=$!
wait $vtouch
sleep 0.5
kill $daemon
sleep 0.3
rm -f /tmp/kt-test
if [ -n "$rotate_user" ]; then
    rotate_to normal
    systemctl --user stop kenzo-rotate-revert.timer
fi
cat vtouch.out
echo "--- журнал демона"
cat kt-test.log
