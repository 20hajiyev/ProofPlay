"""Procedural audio for the M2 slice (plan 2.15, 3.9). Run: python tools/generate_game_audio.py

Everything here is synthesised and labelled as such: no recordings, no samples from elsewhere.
Masters: 48 kHz, 24-bit WAV. Positional sounds mono, music stereo. Deterministic (fixed seeds).
Checks per file (plan 5.5): peak <= -1 dBFS (no clipping), |DC| small, loop seam continuity.
"""
import hashlib, json, math, struct, wave
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
# Own folder: assets/audio/ (other files) belongs to a separate procedural-audio workstream.
OUT = ROOT / 'assets' / 'audio' / 'game'
SR = 48000
PEAK = 10 ** (-1.5 / 20)  # normalise to -1.5 dBFS
rng_master = np.random.default_rng(20260927)


def write_wav(path, data):
    """data: float array (n,) or (n, 2) in [-1, 1] -> 24-bit PCM."""
    path.parent.mkdir(parents=True, exist_ok=True)
    x = np.asarray(data, dtype=np.float64)
    channels = 1 if x.ndim == 1 else x.shape[1]
    ints = np.clip(np.round(x * 8388607.0), -8388608, 8388607).astype(np.int32).reshape(-1)
    raw = ints.astype('<i4').tobytes()
    b24 = b''.join(raw[i:i + 3] for i in range(0, len(raw), 4))
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(3)
        w.setframerate(SR)
        w.writeframes(b24)


def normalise(x, peak=PEAK):
    x = x - np.mean(x, axis=0)  # remove DC
    m = np.max(np.abs(x))
    return x * (peak / m) if m > 0 else x


def circular_noise(n, rng, lo_hz, hi_hz):
    """Band-limited noise that loops seamlessly (filtered in the frequency domain)."""
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    spec[(f < lo_hz) | (f > hi_hz)] = 0
    return np.fft.irfft(spec, n)


def loop_freq(f, n):
    """Nearest frequency with a whole number of cycles in n samples, so a loop of n wraps cleanly."""
    return max(1, round(f * n / SR)) * SR / n


def check(name, x, loop):
    mono = x if x.ndim == 1 else x.mean(axis=1)
    peak = float(np.max(np.abs(x)))
    dc = float(abs(np.mean(mono)))
    r = {'file': name, 'seconds': round(len(x) / SR, 4), 'peak_dbfs': round(20 * math.log10(max(peak, 1e-9)), 2),
         'dc_offset': round(dc, 6), 'clipping': peak >= 0.999}
    if loop:
        # Seam: the jump across the loop point compared with typical sample-to-sample steps.
        steps = np.abs(np.diff(mono))
        seam = abs(float(mono[0] - mono[-1]))
        r['loop_seam_ratio'] = round(seam / (float(np.percentile(steps, 99)) + 1e-12), 3)
        r['loop_ok'] = r['loop_seam_ratio'] < 1.0
    r['ok'] = (not r['clipping']) and dc < 0.002 and r.get('loop_ok', True)
    return r


# --------------------------------------------------------------------------------------------
# Engine loops: 5 steady RPM anchors x 2 load states per family.
FAMILIES = {
    # cylinders, anchors, harmonic tilt (brightness), roughness, turbo whistle
    'I4_NA':    dict(cyl=4, anchors=[1000, 2500, 4000, 5500, 7000], tilt=1.15, rough=0.18, turbo=0.0),
    'I4_TURBO': dict(cyl=4, anchors=[1000, 2500, 4000, 5500, 7200], tilt=1.05, rough=0.14, turbo=0.10),
    'V6':       dict(cyl=6, anchors=[900, 2400, 3900, 5400, 6800], tilt=1.0, rough=0.12, turbo=0.0),
    'V8':       dict(cyl=8, anchors=[800, 2000, 3300, 4700, 6200], tilt=0.85, rough=0.22, turbo=0.0),
}


def engine_loop(fam, rpm, on_load, seed):
    rng = np.random.default_rng(seed)
    crank = rpm / 60.0                          # once-per-rev component (V8 burble lives here)
    periods = max(1, round(1.0 * crank))        # ~1 s, whole crank revolutions
    n = int(round(periods * SR / crank))
    f0 = loop_freq(crank * fam['cyl'] / 2.0, n) # firing frequency of a 4-stroke, whole cycles per loop
    t = np.arange(n) / SR
    # Harmonic body; on-load is brighter and louder in the upper harmonics.
    x = np.zeros(n)
    for h in range(1, 14):
        amp = (1.0 / h ** fam['tilt']) * (1.0 if on_load else (0.55 if h > 2 else 1.0))
        phase = rng.uniform(0, 2 * np.pi)
        x += amp * np.sin(2 * np.pi * f0 * h * t + phase)
    # Sub-harmonics of the crank give each family its character (uneven firing feel).
    for h, amp in [(0.5, 0.25), (1.0, 0.35), (1.5, 0.15)]:
        x += amp * fam['rough'] * 3 * np.sin(2 * np.pi * loop_freq(crank * h * fam['cyl'] / 2, n) * t + rng.uniform(0, 6.28))
    # Per-cycle intensity jitter (periodic inside the loop, so the seam stays clean).
    cycles = int(round(f0 * n / SR))
    jitter = 1.0 + fam['rough'] * rng.standard_normal(max(cycles, 1))
    env = np.interp(t * f0, np.arange(len(jitter)), jitter, period=len(jitter))
    x *= env
    # Intake/exhaust noise, band-limited, looped.
    x += (0.35 if on_load else 0.2) * circular_noise(n, rng, 80, 3000 if on_load else 1500)
    if fam['turbo'] > 0 and on_load:
        whistle = 2000 + rpm * 0.9
        cyc = max(1, round(whistle * n / SR))
        x += fam['turbo'] * np.sin(2 * np.pi * cyc / n * np.arange(n))
    if not on_load:
        # Overrun crackle: sparse pops, placed on the loop so it wraps cleanly.
        for k in range(int(3 + rpm / 1500)):
            pos = rng.integers(0, n - 800)
            pop = np.exp(-np.arange(800) / 90.0) * rng.standard_normal(800) * 0.8
            x[pos:pos + 800] += pop
    return normalise(x, PEAK * (1.0 if on_load else 0.75))


# --------------------------------------------------------------------------------------------
# One-shots.
def env_adsr(n, a, d, s_level, r):
    e = np.ones(n) * s_level
    na, nd, nr = int(a * SR), int(d * SR), int(r * SR)
    e[:na] = np.linspace(0, 1, na, endpoint=False)
    e[na:na + nd] = np.linspace(1, s_level, nd, endpoint=False)
    e[-nr:] *= np.linspace(1, 0, nr)
    return e


def chirp(seconds, f_start, f_end, harmonics=3):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    f = np.geomspace(f_start, f_end, n)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return sum(np.sin(ph * h) / h for h in range(1, harmonics + 1)), n


def burst(seconds, lo, hi, seed, decay):
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    x = circular_noise(n, rng, lo, hi)
    return x * np.exp(-np.arange(n) / (decay * SR))


def sfx_bank():
    s = {}
    x, n = chirp(0.35, 600, 1800); s['pickup'] = x * env_adsr(n, .005, .05, .5, .2)
    x, n = chirp(0.9, 180, 900, 4); s['use_surge'] = x * env_adsr(n, .01, .2, .7, .5) + 0.3 * burst(0.9, 200, 4000, 1, 0.4)
    x, n = chirp(0.5, 1400, 300, 5); s['use_lance'] = x * env_adsr(n, .002, .1, .6, .3)
    s['use_pulse'] = burst(0.7, 40, 900, 2, 0.18) + 0.6 * chirp(0.7, 120, 60, 2)[0] * np.exp(-np.arange(int(.7 * SR)) / (0.2 * SR))
    x, n = chirp(0.3, 400, 250, 2); s['use_trap'] = x * env_adsr(n, .002, .05, .4, .2) + 0.4 * burst(0.3, 1000, 6000, 3, 0.05)
    x, n = chirp(0.12, 2500, 1800, 2); s['use_needle'] = x * env_adsr(n, .001, .03, .4, .08)
    x, n = chirp(0.8, 300, 600, 6); s['use_ward'] = x * env_adsr(n, .05, .2, .6, .4)
    x, n = chirp(0.9, 500, 1000, 3); s['use_mend'] = x * env_adsr(n, .05, .2, .7, .5)
    s['use_storm'] = burst(1.2, 60, 5000, 4, 0.5) * (1 + 0.5 * np.sin(2 * np.pi * 7 * np.arange(int(1.2 * SR)) / SR))
    s['impact_heavy'] = burst(0.8, 30, 3000, 5, 0.15) + 0.8 * chirp(0.8, 90, 40, 3)[0] * np.exp(-np.arange(int(.8 * SR)) / (0.12 * SR))
    s['impact_light'] = burst(0.25, 400, 7000, 6, 0.04)
    x, n = chirp(0.4, 900, 1400, 5); s['block'] = x * env_adsr(n, .002, .05, .5, .3)
    s['wreck'] = burst(1.6, 30, 6000, 7, 0.45)
    x, n = chirp(0.18, 880, 880, 2); s['countdown'] = x * env_adsr(n, .005, .02, .8, .06)
    x, n = chirp(0.45, 1320, 1320, 3); s['go'] = x * env_adsr(n, .005, .05, .8, .2)
    # Threat warning: two-tone, distinct rhythm and pitch so it reads through any mix (plan 2.8).
    w1, n1 = chirp(0.12, 1760, 1760, 2); w2, _ = chirp(0.12, 1320, 1320, 2)
    s['warning'] = np.concatenate([w1, np.zeros(int(0.04 * SR)), w2]) * 1.0
    x, n = chirp(0.06, 1200, 1300, 1); s['ui_move'] = x * env_adsr(n, .002, .01, .6, .03)
    x, n = chirp(0.15, 900, 1600, 2); s['ui_confirm'] = x * env_adsr(n, .002, .02, .7, .08)
    x, n = chirp(0.15, 900, 500, 2); s['ui_back'] = x * env_adsr(n, .002, .02, .7, .08)
    # Car features (D-060): a two-tone horn, the indicator relay tick, a wiper sweep.
    n = int(0.55 * SR); t = np.arange(n) / SR
    tone = sum(np.sign(np.sin(2 * np.pi * f * t)) * 0.5 + 0.5 * np.sin(2 * np.pi * f * t) for f in (415.0, 523.0))
    s['horn'] = tone * env_adsr(n, .01, .05, .9, .08) * 0.5
    s['indicator'] = burst(0.03, 1500, 5000, 41, 0.006)
    s['wiper'] = burst(0.45, 300, 2500, 42, 0.2) * np.sin(np.pi * np.arange(int(0.45 * SR)) / int(0.45 * SR))
    # Performance parts (D-072): the turbo's blow-off valve (a short airy "pssh" that falls in
    # pitch) and exhaust pops on a high-rev lift (a low thump with a crackle on top).
    n = int(0.5 * SR)
    hiss_hi = burst(0.5, 3000, 9000, 51, 0.12)
    hiss_lo = burst(0.5, 900, 3500, 52, 0.22)
    s['blowoff'] = (hiss_hi + 0.8 * hiss_lo) * env_adsr(n, .004, .08, .5, .3)
    for k, sd in enumerate((61, 62)):
        thump = chirp(0.22, 150 - 20 * k, 45, 3)[0] * np.exp(-np.arange(int(0.22 * SR)) / (0.035 * SR))
        crackle = burst(0.22, 700, 6500, sd, 0.018)
        s['backfire' if k == 0 else 'backfire2'] = thump + 0.7 * crackle
    return {k: normalise(v) for k, v in s.items()}


# --------------------------------------------------------------------------------------------
# Harbor music: 128 BPM (plan 3.9 table), 16 bars, four sample-aligned stereo stems.
# Background under engines and combat: soft timbres only (sine bass, dull hats, no metallic
# percussion) and mastered quiet (-12 dBFS peak). Playtest 2026-09-28: the first square-wave /
# 6-14 kHz version was fatiguing ("qulağı ağrıdır").
MUSIC_PEAK = 10 ** (-12 / 20)


def soft(n, attack_s=0.005, release_s=0.02):
    """Attack/release envelope for one note: no clicks, no hard transients."""
    tt = np.arange(n) / SR
    return np.minimum(1, np.minimum(tt / attack_s, (n / SR - tt) / release_s))


def music_harbor(bars=16, bpm=128, seed=11, roots=(45, 45, 48, 43)):
    rng = np.random.default_rng(seed)
    beat = 60.0 / bpm
    n = int(round(bars * 4 * beat * SR))
    stems = {k: np.zeros((n, 2)) for k in ['drums', 'bass', 'harmony', 'intensity']}
    beat_n = beat * SR

    def place(stem, sig, start, pan=0.0):
        # Tails running past the end wrap to the start, so the stem loops without a cut.
        idx = (int(round(start)) + np.arange(len(sig))) % n
        np.add.at(stems[stem][:, 0], idx, sig * (1 - max(pan, 0)))
        np.add.at(stems[stem][:, 1], idx, sig * (1 + min(pan, 0)))

    def hz(midi):
        return 440 * 2 ** ((midi - 69) / 12)

    nk = int(.3 * SR)
    kick = np.sin(2 * np.pi * np.cumsum(np.geomspace(85, 48, nk)) / SR) * np.exp(-np.arange(nk) / (0.08 * SR)) * soft(nk, 0.002, 0.05)
    hat = burst(0.04, 4000, 8000, 21, 0.008) * soft(int(0.04 * SR), 0.001, 0.01)
    clap = burst(0.15, 500, 2500, 22, 0.04) * soft(int(0.15 * SR), 0.002, 0.04)
    roots = list(roots)  # e.g. A, A, C, G (MIDI), one per 4 bars
    for b in range(bars * 4):
        s0 = b * beat_n
        place('drums', kick * 0.9, s0)
        place('drums', hat * 0.12, s0 + beat_n / 2, 0.25)
        if b % 2 == 1:
            place('drums', clap * 0.3, s0, -0.1)
        root = roots[(b // 16) % 4]
        m = int(beat_n * 0.9)  # quarter-note sine bass with a touch of 2nd harmonic
        tt = np.arange(m) / SR
        f = hz(root - 12)
        note = np.sin(2 * np.pi * f * tt) + 0.2 * np.sin(4 * np.pi * f * tt)
        place('bass', note * np.exp(-tt / 0.4) * soft(m, 0.01, 0.05) * 0.5, s0)
    for bar in range(bars):
        root = roots[(bar // 4) % 4]
        chord = [root + 12, root + 15 + (1 if root == 48 else 0), root + 19]
        m = int(4 * beat_n)
        tt = np.arange(m) / SR
        # Two slightly detuned sines per note: warm, slowly swelling pad.
        pad = sum(np.sin(2 * np.pi * hz(c) * d * tt + rng.uniform(0, 6)) for c in chord for d in (0.998, 1.002))
        place('harmony', pad * np.minimum(1, tt / 0.8) * np.minimum(1, (m / SR - tt) / 0.8) * 0.12, bar * 4 * beat_n, -0.2)
        arp = int(beat_n / 2)  # eighth-note pluck, an octave lower than before
        for k in range(8):
            ta = np.arange(arp) / SR
            tone = np.sin(2 * np.pi * hz(chord[k % 3]) * ta)
            place('intensity', tone * np.exp(-ta / 0.12) * soft(arp, 0.006, 0.03) * 0.25, bar * 4 * beat_n + k * arp, 0.3 if k % 2 else -0.3)
        shaker = burst(0.08, 2500, 6000, 30 + bar, 0.02) * soft(int(0.08 * SR), 0.004, 0.02)
        for k in range(8):
            place('intensity', shaker * 0.06, bar * 4 * beat_n + (k + 0.5) * beat_n / 2, 0.4)
    # Same peak for all stems keeps their relative mix; the runtime sets layer volumes.
    peak = max(np.max(np.abs(v)) for v in stems.values())
    return {k: v * (MUSIC_PEAK / peak) - np.mean(v * (MUSIC_PEAK / peak), axis=0) for k, v in stems.items()}, n

# --------------------------------------------------------------------------------------------
def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


report, records = [], []

def emit(rel, data, loop, meta):
    path = OUT / rel
    write_wav(path, data)
    r = check(rel, data, loop)
    report.append(r)
    records.append({'asset_id': rel.replace('/', '_').replace('.wav', '').upper(), 'file': f'assets/audio/game/{rel}', 'sha256': sha(path),
                    'status': 'built', 'provenance': {'method': 'procedural synthesis', 'generator': 'tools/generate_game_audio.py',
                                                      'external_inputs': [], 'is_recording': False}, **meta, 'validation': r})


seed = 100
for fam_id, fam in FAMILIES.items():
    for a, rpm in enumerate(fam['anchors']):
        for load in ['on', 'off']:
            seed += 1
            x = engine_loop(fam, rpm, load == 'on', seed)
            emit(f'engine/{fam_id}/rpm{rpm}_{load}.wav', x, True, {'category': 'engine_loop', 'root_rpm': rpm, 'load': load, 'channels': 1})

for name, x in sfx_bank().items():
    emit(f'sfx/{name}.wav', x, False, {'category': 'sfx', 'channels': 1})

# Turbo whistle loop (D-072): the game pitches and fades it with boost. Whole-cycle sines and
# circular noise, so the 1 s loop wraps without a seam.
n = SR
t = np.arange(n) / SR
whine = (np.sin(2 * np.pi * loop_freq(1950, n) * t) + 0.35 * np.sin(2 * np.pi * loop_freq(3900, n) * t)
         + 0.25 * normalise(circular_noise(n, np.random.default_rng(71), 2000, 7000), 1.0))
emit('loops/turbo_whine.wav', normalise(whine), True, {'category': 'loop', 'channels': 1})

# Radio stations (D-060): the harbour set plus two more moods the player cycles in the car.
for station, bpm, seed_m, roots in (('harbor', 128, 11, (45, 45, 48, 43)), ('nightdrive', 100, 23, (41, 44, 39, 43)), ('rush', 140, 37, (40, 40, 43, 38))):
    stems, frames = music_harbor(bpm=bpm, seed=seed_m, roots=roots)
    for name, x in stems.items():
        emit(f'music/{station}/{name}.wav', x, True, {'category': 'music_stem', 'bpm': bpm, 'bars': 16, 'frames': frames, 'channels': 2})

(OUT / 'engine_families.json').write_text(json.dumps({k: {'cylinders': v['cyl'], 'anchors_rpm': v['anchors']} for k, v in FAMILIES.items()}, indent=2), encoding='utf-8')
(OUT / 'validation_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
(OUT / 'manifest_fragment.json').write_text(json.dumps({'schema_version': 1, 'generator': 'tools/generate_game_audio.py', 'assets': records}, indent=2), encoding='utf-8')
bad = [r for r in report if not r['ok']]
print('AUDIO_COMPLETE', len(report), 'files,', len(bad), 'failed checks')
for r in bad:
    print('FAILED', r)
