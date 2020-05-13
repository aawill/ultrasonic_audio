#!/usr/bin/env python3
import RPi.GPIO as GPIO
import argparse
import sys
import time
import sounddevice as sd
import numpy
import numpy as np
from scipy.signal import hilbert
from scipy.interpolate import interp1d
from matplotlib import pyplot as plt
# ~ import librosa
# ~ from pyrubberband import pyrb
# ~ import soundfile as sf

class DSPTesting:
    def __init__(self, device, sample_rate, buffer_size, dtype, channels):
        print('constructor')
        
        self.device = device
        self.sample_rate = sample_rate
        self.buffer_size = buffer_size
        self.dtype = dtype
        self.channels = channels
        self.iostream = sd.Stream(device=self.device, samplerate=self.sample_rate,
                                  blocksize=self.buffer_size, dtype=self.dtype,
                                  latency='high', channels=self.channels,
                                  callback=self.callback)
        # create empty arrays to store processed audio for plotting
        self.input_data = np.array([], dtype=np.int16)
        self.output_data = np.array([], dtype=np.int16)
        self.sine_start_idx = 0
        self.sine_amplitude = 0.2
        self.sine_freq = 500
        self.delay_gain = 0.9
        self.delay_time = 0.3

        # circular buffer variables
        self.buff_idx = 1
        self.sample_num = 0
        # stores 4 seconds of audio
        self.circular_buffer = np.zeros(self.sample_rate * 4)
        
        # ultrasonic sensor setup
        GPIO.setmode(GPIO.BCM)
        self.MAX_DIST = 45
        self.dist1 = self.MAX_DIST
        self.GPIO_TRIGGER = 17
        self.GPIO_ECHO = 18
        GPIO.setup(self.GPIO_TRIGGER, GPIO.OUT)
        GPIO.setup(self.GPIO_ECHO, GPIO.IN)
        # allows readings of up to about 43 cm
        self.timeout = 0.0025
        
        

    def start_stream(self):
        self.iostream.start()

    def callback(self, indata, outdata, frames, time, status):
        """function to process audio buffers as they become available"""
        if status: 
            print('status:', status)

        #self.dist1 = self.read_sensors()
        #print('dist:', self.dist1)

        # generates a sine wave
        # t = (self.sine_start_idx + np.arange(len(indata))) / self.sample_rate
        # t = t.reshape(-1, 1)
        # sine = (0.01 * np.sin(2 * np.pi * 2000 * t)) * np.iinfo(np.int16).max
        # self.sine_start_idx += len(indata)
        # outdata[:] = self.custom_pitch_shift(sine, smooth=True)

        # used for plotting total processed audio
        # self.input_data = np.append(self.input_data, indata)
        # monooutput = self.pitch_shift(indata)
        # self.output_data = np.append(self.output_data, monooutput)

        # write to stereo output
        #outdata[:] = indata
        outdata[:] = (self.distortion(indata))
        #outdata[:] = self.custom_pitch_shift(indata, smooth=True)

    def delay(self, indata):
        self.sample_num = np.floor(self.sample_rate * self.delay_time)

        write_idx = self.buff_idx
        read_idx = int(write_idx - self.sample_num)

        if read_idx < 0:
            read_idx += len(self.circular_buffer - 1)

        for i in range(len(indata)):
            # put new data into circular buffer
            self.circular_buffer[write_idx] = indata[i]
            # read old data from circ buffer\
            delay_samp = self.circular_buffer[read_idx]
            # add old data to output
            indata[i] += int(delay_samp * self.delay_gain)

            read_idx += 1
            if read_idx >= len(self.circular_buffer):
                read_idx -= len(self.circular_buffer)
            write_idx += 1
            if write_idx >= len(self.circular_buffer):
                write_idx -= len(self.circular_buffer)

        # reset buffer (?)
        self.buff_idx = write_idx

        return indata
            
    def distortion(self, indata):
        # normalize input data to between [-1, 1]
        float_indata = indata.astype('float64')
        float_indata /= np.iinfo(np.int16).max

        amount = 0.7
        k = 2 * amount / (1 - amount)

        # function to apply wave shaping distortion
        distort_sample = lambda samp : (1 + k) * (samp) / (1 + k * abs(samp))

        # run function over input array
        float_outdata = distort_sample(float_indata)

        # return data to its original scale
        return (float_outdata * np.iinfo(np.int16).max).astype('int16')

    def pyrb_pitch_shift(self, indata, smooth=False):
        indata = indata.astype('float64').transpose()[0]

        outdata = pyrb.pitch_shift(indata, self.sample_rate, 7)

        output = outdata.reshape(-1, 1)
        if smooth:
            output = self.smooth_chunk(output, expSmoothing=False)
        return output

    def librosa_pitch_shift(self, indata, smooth=False):
        indata = indata.astype('float64').transpose()[0]

        outdata = librosa.effects.pitch_shift(indata, self.sample_rate, 7)

        output = outdata.reshape(-1, 1)
        if smooth:
            output = self.smooth_chunk(output, expSmoothing=False)
        return output

    def custom_pitch_shift(self, indata, smooth=False):
        # approximately 7 semitones for a 200Hz input
        f_shift = 100
        indata = indata.astype('float64').transpose()[0]

        # zero-padding input signal to maximize fft efficiency - currently unused
        #nextpow2 = lambda x : int(np.ceil(np.log2(np.abs(x))))
        #N_padded = 2**nextpow2(len(indata))
        #t = (self.sine_start_idx + np.arange(N_padded)) / self.sample_rate

        t = (self.sine_start_idx + np.arange(len(indata))) / self.sample_rate
        self.sine_start_idx += len(indata)
        
        output = (hilbert(indata) * np.exp(2j * np.pi * f_shift * t)).real.reshape(-1, 1)
        if smooth:
            output = self.smooth_chunk(output, expSmoothing=False)
        return output

        #padded_sig = np.hstack((indata, np.zeros(N_padded - len(indata), indata.dtype)))
        #output = (hilbert(padded_sig) * np.exp(2j * np.pi * f_shift * t))[:N_padded].real
        #return output

    def smooth_chunk(self, chunk, expSmoothing=False):
        #print(len(chunk))
        fade_coefficient = 0.0
        smoothing_distance = 100

        chunk_start = chunk[:smoothing_distance]
        chunk_end = chunk[len(chunk) - smoothing_distance:]
        chunk_end = np.flip(chunk_end)

        for i in range(smoothing_distance):
            start_sample = chunk_start[i]
            end_sample = chunk_end[i]

            start_sample *= fade_coefficient
            end_sample *= fade_coefficient

            if expSmoothing:
                fade_coefficient = np.power(2, i / smoothing_distance) - 1
            else:
                fade_coefficient += 1 / smoothing_distance
                
        chunk[:smoothing_distance] = chunk_start
        chunk[len(chunk) - smoothing_distance:] = np.flip(chunk_end)

        return chunk

    def plot_data(self):
        """plots whatever audio has been processed so far"""
        # plot against time in seconds instead of samples
        input_time_axis = np.arange(len(self.input_data)) / self.sample_rate

        output_time_axis = np.arange(len(self.output_data)) / self.sample_rate

        # normalize data to between [-1, 1]
        float_input = self.input_data.astype('float64')
        float_input /= np.iinfo(np.int16).max

        float_output = self.output_data.astype('float64')
        float_output /= np.iinfo(np.int16).max

        # in case time axis and data are slightly different lengths
        if len(input_time_axis) < len(float_input):
            float_input.resize(len(input_time_axis))
        elif len(float_input) < len(input_time_axis):
            input_time_axis.resize(len(float_input))
        
        if len(output_time_axis) < len(float_output):
            float_output.resize(len(output_time_axis))
        elif len(float_output) < len(output_time_axis):
            output_time_axis.resize(len(float_output))

        fig, axs = plt.subplots(2)
        fig.suptitle('Dry and wet audio')
        axs[0].plot(input_time_axis, float_input)
        axs[1].plot(output_time_axis, float_output)

        plt.show()

    def read_sensors():
        # set Trigger to HIGH
        GPIO.output(self.GPIO_TRIGGER, True)
     
        # set Trigger after 0.01ms to LOW
        time.sleep(0.00001)
        GPIO.output(self.GPIO_TRIGGER, False)
     
        StartTime = time.time()
        StopTime = time.time()
     
        # save StartTime
        while GPIO.input(self.GPIO_ECHO) == 0:
            StartTime = time.time()
     
        # save time of arrival
        while GPIO.input(self.GPIO_ECHO) == 1 and time.time() - StartTime < timeout:
            StopTime = time.time()
     
        # time difference between start and arrival
        TimeElapsed = StopTime - StartTime
        # multiply with the sonic speed (34300 cm/s)
        # and divide by 2, because there and back
        distance = (TimeElapsed * 34300) / 2
     
        return distance
        
try:

    input_device = 'AudioInjector'
    output_device = 'AudioInjector'

    tester = DSPTesting((input_device, output_device), 44100, 0, np.int16, (1, 2))

    tester.start_stream()
    print('#' * 80)
    print('press Return to quit')
    print('#' * 80)
    input()

    GPIO.cleanup()
    # chunk_size = 1024
    # num_chunks = 20
    # sine_freq = 200
    # amplitude = 0.2
    # t = np.arange(chunk_size * num_chunks) / tester.sample_rate
    # sig = (amplitude * np.sin(2 * np.pi * sine_freq * t)).reshape(-1, 1)
    # chunks = np.split(sig, num_chunks)

    # pyrb_shift_offline = tester.pyrb_pitch_shift(sig)
    # librosa_shift_offline = tester.librosa_pitch_shift(sig)
    # custom_shift_offline = tester.custom_pitch_shift(sig)

    # pyrb_shift_chunks = np.array([])
    # librosa_shift_chunks = np.array([])
    # custom_shift_chunks = np.array([])
    # custom_lin_smoothed_chunks = np.array([])
    # custom_exp_smoothed_chunks = np.array([])
    # for chunk in chunks:
    #     #pyrb_shift_chunks = np.append(pyrb_shift_chunks, tester.pyrb_pitch_shift(chunk))
    #     #librosa_shift_chunks = np.append(librosa_shift_chunks, tester.librosa_pitch_shift(chunk))
    #     custom_chunk = tester.custom_pitch_shift(chunk)
    #     custom_shift_chunks = np.append(custom_shift_chunks, custom_chunk)
    #     custom_lin_smoothed_chunks = np.append(custom_lin_smoothed_chunks, tester.smooth_chunk(custom_chunk))
    #     custom_exp_smoothed_chunks = np.append(custom_exp_smoothed_chunks, tester.smooth_chunk(custom_chunk, expSmoothing=True))

    # sf.write('soundfiles/custom_unsmoothed.wav', custom_shift_chunks, tester.sample_rate)
    # sf.write('soundfiles/custom_smoothed.wav', custom_smoothed_chunks, tester.sample_rate)

    # _, axs0 = plt.subplots(3, sharex=True)
    # axs0[0].set_title('unsmoothed signal')
    # axs0[0].plot(custom_shift_chunks)
    # axs0[1].set_title('linear smoothed signal')
    # axs0[1].plot(custom_lin_smoothed_chunks)
    # axs0[2].set_title('exp smoothed signal')
    # axs0[2].plot(custom_exp_smoothed_chunks)

    # sf.write('soundfiles/original.wav', sig, tester.sample_rate)
    # sf.write('soundfiles/pyrb_shift_offline.wav', pyrb_shift_offline, tester.sample_rate)
    # sf.write('soundfiles/pyrb_shift_buffered.wav', pyrb_shift_chunks, tester.sample_rate)
    # sf.write('soundfiles/librosa_shift_offline.wav', librosa_shift_offline, tester.sample_rate)
    # sf.write('soundfiles/librosa_shift_buffered.wav', librosa_shift_chunks, tester.sample_rate)
    # sf.write('soundfiles/custom_shift_offline.wav', custom_shift_offline, tester.sample_rate)
    # sf.write('soundfiles/custom_shift_buffered.wav', custom_shift_chunks, tester.sample_rate)

    # _, axs = plt.subplots(3, sharex=True)
    # axs[0].set_title('original signal')
    # axs[0].plot(sig)
    # axs[1].set_title('pitch-shifted signal (offline)')
    # axs[1].plot(pyrb_shift_offline)
    # axs[2].set_title('pitch-shifted signal (buffered)')
    # axs[2].plot(pyrb_shift_chunks)
    # axs[2].set_xlabel('time (samples)')
    # axs[2].set_ylabel('amplitude')
    # 
    # _, axs2 = plt.subplots(3, sharex=True)
    # axs2[0].set_title('original signal')
    # axs2[0].plot(sig)
    # axs2[1].set_title('pitch-shifted signal (offline)')
    # axs2[1].plot(librosa_shift_offline)
    # axs2[2].set_title('pitch-shifted signal (buffered)')
    # axs2[2].plot(librosa_shift_chunks)
    # axs2[2].set_xlabel('time (samples)')
    # axs2[2].set_ylabel('amplitude')
    # 
    # _, axs3 = plt.subplots(3, sharex=True)
    # axs3[0].set_title('original signal')
    # axs3[0].plot(sig)
    # axs3[1].set_title('pitch-shifted signal (offline)')
    # axs3[1].plot(custom_shift_offline)
    # axs3[2].set_title('pitch-shifted signal (buffered)')
    # axs3[2].plot(custom_lin_smoothed_chunks)
    # axs3[2].set_xlabel('time (samples)')
    # axs3[2].set_ylabel('amplitude')
    # 
    # plt.show()

    # while True:
    #     data = tester.iostream.read(256)[0].transpose()[0]
    #     data = np.fft.rfft(data)
    #     data = np.roll(data, 2)
    #     data[0:5] = 0
    #     data = np.fft.irfft(data)
    #     dataout = np.array(data, dtype='int16')
    #     #output = struct.pack("%dh"%(len(dataout)), *list(dataout)) 
    #     tester.iostream.write(dataout)

    # random_buffer = np.random.rand(500) * np.iinfo(np.int16).max
    # tic = time.perf_counter()
    # for i in range(500):
    #     tester.distortion(random_buffer)
    # toc = time.perf_counter()
    # print(f"Ran distortion algorithm 500 times in {toc - tic:0.4f} seconds")

    #tester.plot_data()
    
except KeyboardInterrupt:
    print("Measurement stopped by User")
    GPIO.cleanup()

