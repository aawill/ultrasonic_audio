#!/usr/bin/env python3
"""Play a sine signal."""
import argparse
import sys

import math
import numpy as np
#from scipy.fftpack import fft
from scipy.signal import hilbert
import sounddevice as sd
from matplotlib import pyplot as plt 
import scipy.io.wavfile as wave
import librosa
from pyrubberband import pyrb

start_idx = 0
sample_rate = 44100
indata = np.zeros(2048)
amplitude = 0.2
frequency = 200
carrier_freq = 1000

def gen_sine(freq):
    return amplitude * np.sin(2 * np.pi * freq * t)

def gen_cosine(freq):
    return amplitude * np.cos(2 * np.pi * freq * t)

t = (start_idx + np.arange(len(indata) * 16)) / sample_rate
dt = 1 / sample_rate
shift = 2000

sig = gen_sine(200)
sig2 = gen_sine(400)
sig3 = gen_sine(100)

com_sig = sig

freq_axis = np.fft.fftfreq(len(sig), 1 / sample_rate)
pos_freq_axis = freq_axis[:len(freq_axis) // 2 + 1]

# sig_fft = np.abs(np.fft.rfft(sig))
# rolled_fft = np.roll(sig_fft, shift)
# rolled_fft[0:shift] = 0

# _, axs = plt.subplots(2, sharex=True)
# axs[0].plot(pos_freq_axis, sig_fft)
# axs[1].plot(pos_freq_axis, rolled_fft)

# plt.xlim(0, 5000)

def nextpow2(x):
    """Return the first integer N such that 2**N >= abs(x)"""

    return int(np.ceil(np.log2(np.abs(x))))

def freq_shift(x, f_shift, dt):
    """
    Shift the specified signal by the specified frequency.
    """

    # Pad the signal with zeros to prevent the FFT invoked by the transform from
    # slowing down the computation:
    
    N_orig = len(x)
    N_padded = 2**nextpow2(N_orig)

    t = np.arange(0, N_orig) / sample_rate
    return (hilbert(x) ** (3/2))
    #return (hilbert(x) * np.exp(2j * np.pi * f_shift * t))

    #return (hilbert(np.hstack((x, np.zeros(N_padded-N_orig, x.dtype))))*np.exp(2j*np.pi*f_shift*t))[:N_padded].real

#shifted_sig = freq_shift(sig, 100, dt)

#shifted_sig = np.fft.irfft(rolled_fft)

shifted_sig = freq_shift(com_sig, shift, dt)

# y, sr = librosa.load('sine.wav', sr=44100)
# shifted_sig = librosa.effects.pitch_shift(y, sample_rate, 4)

sig_fft = abs(np.fft.rfft(sig))
com_sig_fft = abs(np.fft.rfft(com_sig))
shifted_fft = abs(np.fft.rfft(shifted_sig))

_, axs = plt.subplots(2, sharex=True)
axs[0].plot(pos_freq_axis, com_sig_fft)
axs[1].plot(pos_freq_axis, shifted_fft)

plt.xlim(0, 3000)

# _, axs2 = plt.subplots(2, sharex=True)
# axs2[0].plot(sig)
# axs2[1].plot(shifted_sig)

plt.show()