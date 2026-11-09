// Частота тона в записи с телефона: верно ли идут часы кодека (26b08-02, шаг 1 сценария 09.11).
//   node pitch.mts gen <файл.wav> [Гц=1000] [секунды=20]   — тон для afplay на Маке
//   node pitch.mts gen-lr <файл.wav> [Гц Л=500] [Гц П=1000] [с=2] — сначала только левый, потом только правый
//   node pitch.mts gen-s24 <файл.raw> [Гц=1000] [с=3]       — сырой S24_LE стерео для aplay -f S24_LE -t raw
//   node pitch.mts measure <mic.wav>                       — пик спектра по всей записи и по секундам, уровень тона
// Мера — алгоритм Гёрцеля: грубый поиск пика 200–4000 Гц шагом 2 Гц по первой полной секунде, затем
// уточнение ±3 Гц шагом 0,02 Гц по всей записи без первой и последней половины секунды.
import { readFileSync, writeFileSync } from "node:fs";

class Wav {
	readonly rate: number;
	readonly samples: Float64Array;

	constructor(rate: number, samples: Float64Array) {
		this.rate = rate;
		this.samples = samples;
	}

	static read(path: string): Wav {
		const b = readFileSync(path);
		let pos = 12;
		let rate = 0;
		let channels = 1;
		let bits = 16;
		while (pos + 8 <= b.length) {
			const id = b.toString("ascii", pos, pos + 4);
			const size = b.readUInt32LE(pos + 4);
			const body = pos + 8;
			if (id === "fmt ") {
				channels = b.readUInt16LE(body + 2);
				rate = b.readUInt32LE(body + 4);
				bits = b.readUInt16LE(body + 14);
			} else if (id === "data") {
				if (bits !== 16) throw new Error(`нужен 16-битный PCM, а тут ${bits}`);
				// arecord пишет размер 0x7fffffff, если не знал длину заранее — берём до конца файла
				const end = Math.min(b.length, body + size);
				const frames = Math.floor((end - body) / (2 * channels));
				const s = new Float64Array(frames);
				for (let i = 0; i < frames; i++) s[i] = b.readInt16LE(body + i * 2 * channels) / 32768;
				return new Wav(rate, s);
			}
			pos = body + size + (size & 1);
		}
		throw new Error("нет блока data");
	}

	static writeTone(path: string, hz: number, seconds: number): void {
		Wav.writeLeftRight(path, hz, hz, seconds, false);
	}

	/** Стерео: hz1 в обоих каналах, затем hz2 в обоих — проверка, теряется ли начало потока, а не канал. */
	static writeSequence(path: string, hz1: number, hz2: number, seconds: number): void {
		const a = Wav.toneSamples(hz1, seconds);
		const b = Wav.toneSamples(hz2, seconds);
		const frames = a.length + b.length;
		const buf = Wav.header(frames);
		for (let i = 0; i < frames; i++) {
			const v = i < a.length ? a[i] : b[i - a.length];
			buf.writeInt16LE(v, 44 + i * 4);
			buf.writeInt16LE(v, 46 + i * 4);
		}
		writeFileSync(path, buf);
	}

	/**
	 * Сырой стерео-поток S24_LE (как его отдаёт ALSA: 24-битный отсчёт в младших битах 32-битного слова, знак
	 * расширен) с тем же уровнем, что у 16-битного тона: для aplay -f S24_LE -c 2 -r 48000 -t raw.
	 */
	static writeRawS24(path: string, hz: number, seconds: number): void {
		const s = Wav.toneSamples(hz, seconds);
		const b = Buffer.alloc(s.length * 8);
		for (let i = 0; i < s.length; i++) {
			b.writeInt32LE(s[i] * 256, i * 8);
			b.writeInt32LE(s[i] * 256, i * 8 + 4);
		}
		writeFileSync(path, b);
	}

	/** Отсчёты тона 48 кГц, амплитуда 0,3, нарастание и спад по 20 мс. */
	static toneSamples(hz: number, seconds: number): Int16Array {
		const rate = 48000;
		const n = Math.round(rate * seconds);
		const fade = rate * 0.02;
		const s = new Int16Array(n);
		for (let i = 0; i < n; i++) {
			const env = Math.min(1, i / fade, (n - 1 - i) / fade);
			s[i] = Math.round(0.3 * env * Math.sin((2 * Math.PI * hz * i) / rate) * 32767);
		}
		return s;
	}

	/** Заголовок WAV: 48 кГц, 16 бит, стерео, frames кадров; данные — с байта 44. */
	static header(frames: number): Buffer {
		const rate = 48000;
		const b = Buffer.alloc(44 + frames * 4);
		b.write("RIFF", 0, "ascii");
		b.writeUInt32LE(36 + frames * 4, 4);
		b.write("WAVEfmt ", 8, "ascii");
		b.writeUInt32LE(16, 16);
		b.writeUInt16LE(1, 20);
		b.writeUInt16LE(2, 22);
		b.writeUInt32LE(rate, 24);
		b.writeUInt32LE(rate * 4, 28);
		b.writeUInt16LE(4, 32);
		b.writeUInt16LE(16, 34);
		b.write("data", 36, "ascii");
		b.writeUInt32LE(frames * 4, 40);
		return b;
	}

	/** Стерео: при split — сначала hzLeft только в левом канале, потом hzRight только в правом; иначе оба сразу. */
	static writeLeftRight(path: string, hzLeft: number, hzRight: number, seconds: number, split: boolean): void {
		const rate = 48000;
		const part = rate * seconds;
		const frames = split ? part * 2 : part;
		const fade = rate * 0.02;
		const b = Buffer.alloc(44 + frames * 4);
		b.write("RIFF", 0, "ascii");
		b.writeUInt32LE(36 + frames * 4, 4);
		b.write("WAVEfmt ", 8, "ascii");
		b.writeUInt32LE(16, 16);
		b.writeUInt16LE(1, 20);
		b.writeUInt16LE(2, 22);
		b.writeUInt32LE(rate, 24);
		b.writeUInt32LE(rate * 4, 28);
		b.writeUInt16LE(4, 32);
		b.writeUInt16LE(16, 34);
		b.write("data", 36, "ascii");
		b.writeUInt32LE(frames * 4, 40);
		for (let i = 0; i < frames; i++) {
			// у каждого отрезка своё нарастание и спад — без щелчка на стыке каналов
			const right = split && i >= part;
			const j = right ? i - part : i;
			const len = split ? part : frames;
			const env = Math.min(1, j / fade, (len - 1 - j) / fade);
			const hz = right ? hzRight : hzLeft;
			const v = Math.round(0.3 * env * Math.sin((2 * Math.PI * hz * j) / rate) * 32767);
			b.writeInt16LE(split && right ? 0 : v, 44 + i * 4);
			b.writeInt16LE(split && !right ? 0 : v, 46 + i * 4);
		}
		writeFileSync(path, b);
	}
}

class Pitch {
	/** Квадрат модуля спектра на частоте hz по отрезку [from, to). */
	static power(s: Float64Array, rate: number, hz: number, from: number, to: number): number {
		const k = 2 * Math.cos((2 * Math.PI * hz) / rate);
		let a = 0;
		let p = 0;
		for (let i = from; i < to; i++) {
			const n = s[i] + k * a - p;
			p = a;
			a = n;
		}
		return a * a + p * p - k * a * p;
	}

	static peak(s: Float64Array, rate: number, from: number, to: number, lo: number, hi: number, step: number): { hz: number; power: number } {
		let best = { hz: lo, power: -1 };
		for (let hz = lo; hz <= hi; hz += step) {
			const power = Pitch.power(s, rate, hz, from, to);
			if (power > best.power) best = { hz, power };
		}
		return best;
	}

	static measure(path: string): void {
		const w = Wav.read(path);
		const s = w.samples;
		const r = w.rate;
		const sec = s.length / r;
		let sum = 0;
		let peakAbs = 0;
		let zeroRun = 0;
		let longestZeroRun = 0;
		for (const v of s) {
			sum += v * v;
			peakAbs = Math.max(peakAbs, Math.abs(v));
			zeroRun = v === 0 ? zeroRun + 1 : 0;
			longestZeroRun = Math.max(longestZeroRun, zeroRun);
		}
		console.log(`файл: ${r} Гц, ${sec.toFixed(2)} с; уровень RMS ${(20 * Math.log10(Math.sqrt(sum / s.length) + 1e-12)).toFixed(1)} дБFS, пик ${(20 * Math.log10(peakAbs + 1e-12)).toFixed(1)} дБFS; самая длинная серия нулей ${longestZeroRun} отсчётов`);
		const from = Math.floor(r * 0.5);
		const to = s.length - Math.floor(r * 0.5);
		if (to - from < r) throw new Error("запись короче двух секунд");
		const coarse = Pitch.peak(s, r, from, from + r, 200, 4000, 2);
		const fine = Pitch.peak(s, r, from, to, coarse.hz - 3, coarse.hz + 3, 0.02);
		// фон — медиана мощности по сетке 200–4000 Гц на той же секунде
		const grid: number[] = [];
		for (let hz = 200; hz <= 4000; hz += 7) grid.push(Pitch.power(s, r, hz, from, from + r));
		grid.sort((x, y) => x - y);
		const snr = 10 * Math.log10(coarse.power / (grid[Math.floor(grid.length / 2)] + 1e-30));
		const amp = (2 * Math.sqrt(fine.power)) / (to - from);
		console.log(`пик: ${fine.hz.toFixed(2)} Гц (грубо ${coarse.hz} Гц, над фоном ${snr.toFixed(1)} дБ), уровень тона ${(20 * Math.log10(amp + 1e-12)).toFixed(1)} дБFS`);
		const perSecond: string[] = [];
		for (let a = from; a + r <= to; a += r) {
			const p = Pitch.peak(s, r, a, a + r, fine.hz - 3, fine.hz + 3, 0.05);
			perSecond.push(p.hz.toFixed(2));
		}
		console.log(`по секундам: ${perSecond.join(" ")}`);
	}

	static main(args: string[]): void {
		const [cmd, path, a, b] = args;
		if (cmd === "gen" && path) {
			Wav.writeTone(path, Number(a ?? 1000), Number(b ?? 20));
			console.log(`тон ${a ?? 1000} Гц, ${b ?? 20} с → ${path}`);
		} else if (cmd === "gen-s24" && path) {
			Wav.writeRawS24(path, Number(a ?? 1000), Number(b ?? 3));
			console.log(`сырой S24_LE стерео: ${a ?? 1000} Гц, ${b ?? 3} с → ${path}`);
		} else if (cmd === "gen-seq" && path) {
			const [, , h1, h2, sec] = args;
			Wav.writeSequence(path, Number(h1 ?? 500), Number(h2 ?? 1000), Number(sec ?? 2));
			console.log(`оба канала: ${h1 ?? 500} Гц ${sec ?? 2} с, затем ${h2 ?? 1000} Гц ${sec ?? 2} с → ${path}`);
		} else if (cmd === "gen-lr" && path) {
			const [, , l, r, sec] = args;
			Wav.writeLeftRight(path, Number(l ?? 500), Number(r ?? 1000), Number(sec ?? 2), true);
			console.log(`левый ${l ?? 500} Гц ${sec ?? 2} с, затем правый ${r ?? 1000} Гц ${sec ?? 2} с → ${path}`);
		} else if (cmd === "measure" && path) {
			Pitch.measure(path);
		} else {
			console.error("node pitch.mts gen <файл.wav> [Гц] [секунды] | gen-lr <файл.wav> [Гц левый] [Гц правый] [секунды] | measure <mic.wav>");
			process.exitCode = 2;
		}
	}
}

Pitch.main(process.argv.slice(2));
