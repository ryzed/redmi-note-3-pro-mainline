#!/bin/sh
# kenzo, mainline: тон для прослушивания напрямую через ALSA. Динамик — RX0 → RX INT6_2 →
# LINEOUT4 → усилитель; наушники 3,5 мм — как путь headphones в mixer_paths kenzo (RX0 → RX INT1_2 → HPHL,
# RX1 → RX INT2_2 → HPHR, DEM MUX = CLSH_DSM_OUT), динамик выключен. Тише эталона: пользователь просит пробы на
# слух тише и каждый запуск своим тоном. Маршрут — аргументом; PipeWire перед запуском остановить.
# Звук — готовый WAV точной длины (pitch.mts gen / gen-lr на Маке), а не speaker-test: тот играет каналы по
# очереди, и половина времени — тишина второго канала. Регистров не читает: файл registers интерфейса читается
# по шине целиком ~20 с и растягивает паузы между тонами (09.11).
# Запуск: sudo sh snd-play.sh <тон.wav> [громкость, 84 = 0 дБ, шаг 1 дБ; динамик 72, наушники 54] [speaker|headphones]
WAV=${1:?путь к WAV}
ROUTE=${3:-speaker}
M() { amixer -q -c 0 cset name="$1" "$2" || echo "amixer fail: $1 = $2"; }
M 'SLIMBUS_0_RX Audio Mixer MultiMedia1' 1
M 'RX INT5_1 MIX1 INP0' ZERO
M 'RX INT6_1 MIX1 INP0' ZERO
M 'RX INT6_1 MIX1 INP1' ZERO
M 'RX INT1_1 MIX1 INP0' ZERO
M 'RX INT2_1 MIX1 INP0' ZERO
if [ "$ROUTE" = headphones ]; then
	VOL=${2:-54}
	M 'Speaker Switch' off
	M 'RX INT6_2 MUX' ZERO
	M 'SLIM RX0 MUX' AIF1_PB
	M 'SLIM RX1 MUX' AIF1_PB
	M 'RX INT1_2 MUX' RX0
	M 'RX INT2_2 MUX' RX1
	M 'RX INT1 DEM MUX' CLSH_DSM_OUT
	M 'RX INT2 DEM MUX' CLSH_DSM_OUT
	M 'RX1 Mix Digital Volume' "$VOL"
	M 'RX2 Mix Digital Volume' "$VOL"
else
	VOL=${2:-72}
	M 'RX INT1_2 MUX' ZERO
	M 'RX INT2_2 MUX' ZERO
	M 'SLIM RX0 MUX' AIF1_PB
	M 'SLIM RX1 MUX' ZERO
	M 'RX INT6_2 MUX' RX0
	M 'COMP6 Switch' on
	M 'RX6 Digital Volume' "$VOL"
	M 'RX6 Mix Digital Volume' "$VOL"
	M 'LINEOUT4 Volume' 13
	M 'Speaker Switch' on
fi
MARK="snd-play $ROUTE $(basename "$WAV") $(date +%s)"
echo "$MARK" > /dev/kmsg
echo "$(cut -d' ' -f1 /proc/uptime) start $ROUTE $WAV"
aplay -q -D hw:0,0 "$WAV"
echo "$(cut -d' ' -f1 /proc/uptime) end, aplay exit $?"
dmesg | sed -n "/$MARK/,\$p" | grep -vE 'wcn36xx|wlan'
