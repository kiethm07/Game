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

gen_coin()
gen_hit()
gen_dash()
print("Generated placeholder audio files in assets/audio/")
