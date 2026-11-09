#!/bin/sh
# kenzo: наблюдение за гнездом наушников (MBHC, тикет 26b10-01). Раз в 0,3 с — контрол «Headset Jack» и
# счётчики прерываний MBHC; в журнал — только изменения, с временем от загрузки (сверять с dmesg).
# Запуск отдельной службой (переживает обрыв Wi-Fi; вывод без буфера — echo пишет строку сразу):
#   sudo systemd-run --unit=jack-watch --collect -p StandardOutput=truncate:/root/jack-watch.log \
#     sh /home/user/jack-watch.sh 120
# Ни evtest, ни конвейер через sed здесь не годятся: при обрыве буферизованный вывод теряется (опыт FP3).
N=${1:-60}
now() { cut -d' ' -f1 /proc/uptime; }
end=$(( $(cut -d. -f1 /proc/uptime) + N ))
echo "$(now) start, irq:"
grep -iE 'mbhc|button|elect|ocp' /proc/interrupts | awk '{n=$1; sub(/:$/, "", n); $0=$0; for (i=2; i<=NF && $i ~ /^[0-9]+$/; i++); s=""; for (; i<=NF; i++) s=s" "$i; print "  " n s}'
prev=
while [ "$(cut -d. -f1 /proc/uptime)" -lt "$end" ]; do
	jack=$(amixer -c0 cget iface=CARD,name='Headset Jack' 2>/dev/null | sed -n 's/.*: values=//p')
	irq=$(grep -iE 'mbhc|button|elect|ocp' /proc/interrupts |
		awk '{n=$1; sub(/:$/, "", n); s=0; for (i=2; i<=NF && $i ~ /^[0-9]+$/; i++) s+=$i; printf "%s=%d ", n, s}')
	line="jack=$jack $irq"
	if [ "$line" != "$prev" ]; then
		echo "$(now) $line"
		prev=$line
	fi
	sleep 0.3
done
echo "$(now) end"
