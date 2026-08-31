import wave
import struct
import math
import random
import os

os.makedirs('assets/audio', exist_ok=True)

def save_wav(filename, samples, sample_rate=44100):
    with wave.open(filename, 'w') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        for s in samples:
            w.writeframes(struct.pack('<h', int(s * 32767.0)))

def gen_coin():
    samples = []
    # High pitched sine wave with fast decay
    freq = 1200.0
    for i in range(44100 // 4): # 0.25 seconds
        t = i / 44100.0
        env = math.exp(-15.0 * t)
        s = math.sin(2 * math.pi * freq * t) * env
        samples.append(s * 0.5)
    save_wav('assets/audio/coin.wav', samples)

def gen_hit():
    samples = []
    # White noise with fast decay
    for i in range(44100 // 5): # 0.2 seconds
        t = i / 44100.0
        env = math.exp(-25.0 * t)
        s = random.uniform(-1, 1) * env
        samples.append(s * 0.5)
    save_wav('assets/audio/hit.wav', samples)

def gen_dash():
    samples = []
    # Sweeping down frequency
    freq = 800.0
    for i in range(44100 // 3): # 0.33 seconds
        t = i / 44100.0
        env = math.exp(-10.0 * t)
        f = freq * math.exp(-5.0 * t)
        s = math.sin(2 * math.pi * f * t) * env
        samples.append(s * 0.4)
    save_wav('assets/audio/dash.wav', samples)

def gen_block():
    # A guard absorbing a blow, as against a deflect throwing one off.
    #
    # "Less sharp" is three things, and the attack ramp is the biggest of them:
    # the deflect MP3s open on an instant edge, so a 7 ms ramp alone takes most
    # of the bite out. Then the partials sit low and inharmonic -- a guard being
    # driven back, not a blade ringing -- and a one-pole lowpass at 1.4 kHz
    # rolls off the bright top the deflects keep.
    sample_rate = 44100
    duration = 0.38
    count = int(sample_rate * duration)

    # freq, amplitude, decay. Inharmonic on purpose: exact ratios would read as
    # a pitched note rather than as an impact.
    partials = [(196.0, 1.00, 9.0), (271.0, 0.70, 11.0), (383.0, 0.45, 14.0),
                (525.0, 0.25, 18.0), (742.0, 0.12, 24.0)]

    alpha = 1.0 - math.exp(-2.0 * math.pi * 1400.0 / sample_rate)
    lowpass = 0.0
    samples = []
    for i in range(count):
        t = i / sample_rate
        s = 0.0
        for freq, amp, decay in partials:
            s += math.sin(2 * math.pi * freq * t) * amp * math.exp(-decay * t)
        # The impact itself: noise, gone almost immediately.
        s += random.uniform(-1, 1) * 0.9 * math.exp(-45.0 * t)
        s *= min(1.0, t / 0.007)   # soft attack
        lowpass += alpha * (s - lowpass)
        samples.append(lowpass)

    peak = max(abs(s) for s in samples) or 1.0
    samples = [s / peak * 0.72 for s in samples]
    save_wav('assets/audio/block.wav', samples)


gen_coin()
gen_hit()
gen_dash()
gen_block()
print("Generated placeholder audio files in assets/audio/")
