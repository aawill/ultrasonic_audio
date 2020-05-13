import alsaaudio as aa
import numpy as np
import struct
from gpiozero import DistanceSensor
import time
import threading
from scipy import signal
from matplotlib import pyplot as plt


class UltrasonicMusic:
    def __init__(self, sample_rate, buffer_size):
        """
        Class to contain functionality for the ultrasonic effects processing device.
        
        Attributes:
        sample_rate (int): how many samples of audio to read from the ADC per second
        buffer_size (int): how large of chunks to process at a time
        """
        self.sample_rate = sample_rate
        self.buffer_size = buffer_size
        # each sample has length 2
        self.adjusted_sample_rate = self.sample_rate * 2
        
        # I/O device interfaces
        self.input_device = aa.PCM(type=aa.PCM_CAPTURE, mode=aa.PCM_NORMAL, cardindex=0)
        self.input_device.setrate(self.sample_rate)
        self.input_device.setperiodsize(self.buffer_size)
        
        self.output_device = aa.PCM(type=aa.PCM_PLAYBACK, mode=aa.PCM_NORMAL, cardindex=0)
        self.output_device.setrate(self.sample_rate)
        self.output_device.setperiodsize(self.buffer_size)

        # parameters of audio effects
        self.DISTORTION_MIN = 0
        self.DISTORTION_MAX = 0.8
        self.distortion_amount = 0
        self.DELAY_SECONDS_MIN = 0
        self.DELAY_SECONDS_MAX = 0.4
        self.delay_seconds = 0
        self.delay_samples = int(np.floor(self.adjusted_sample_rate * self.delay_seconds))
        self.delay_gain = 0.4

        # stores 4 seconds of audio for use in delay effect
        self.circ_buff = np.zeros(self.adjusted_sample_rate * 4)  
        # location of read and write indices into circular buffer
        self.delay_read_idx = 0
        self.write_idx = 0
        
        self.changing_delay = False
                      
        self.doppler_read_idx = 0
        t = np.linspace(0, self.buffer_size / self.sample_rate, self.buffer_size)
        self.sawtooth = signal.sawtooth(2*np.pi*1040*t)
        
        
        Bparam, Aparam = signal.iirfilter(2, 0.02,
                                          btype='lowpass', analog=False, ftype='butter')
        Z, P, K = signal.tf2zpk(Bparam, Aparam)
        self.sos = signal.zpk2sos(Z, P, K)
        self.sos_conditions = np.zeros((self.sos.shape[0], 2))
        
        # ultrasonic sensor variables
        self.MAX_DIST = 0.35
        self.delay_dist_tolerance = 0.03
        self.distortion_dist = self.MAX_DIST
        self.delay_dist = self.MAX_DIST
        self.distortion_sensor = DistanceSensor(trigger=17, echo=27,
                                      max_distance=0.35, queue_len=5)
        self.delay_sensor = DistanceSensor(trigger=23, echo=24,
                                      max_distance=0.35 + self.delay_dist_tolerance, queue_len=5) 
        
        # separate thread to read from sensors
        self.sensor_thread = threading.Thread(target=self.read_sensors)
        
        #self.vfunc_single_delay = np.vectorize(self.single_delay)
        
    def __del__(self):
        print('destructing')
        #self.sensor_thread.join()

    def setup(self):
        """Prepare tester to run."""
        
        # preload buffer with four chunks to avoid wacky noises on startup
        preload_data = np.zeros(self.buffer_size * 2 * 4, dtype='int16')
        preload_data = struct.pack('%dh'%(len(preload_data)), *list(preload_data))
        self.output_device.write(preload_data)
        # start reading from sensors
        #self.sensor_thread.start()

    def process(self, read_info):
        """
        Apply effects to a chunk of audio.
        
        Parameters:
        read_info (int, bytestring): the results of calling input_device.read().
                                     should be a tuple containing (frames, data)
                                     where frames is the number of samples read
                                     and data is the samples in bytestring form.
        Returns:
        bytestring: the processed chunk
        """
        # unpack data, convert to a numpy array
        frames, inbuffer = read_info
        indata = np.frombuffer(inbuffer, dtype=np.int16)
        # change to float for processing
        indata = np.array(indata, dtype=np.float64)
        
#         # apply delay. if the value of delay_samples has been changed this iteration,
#         # additional processing is needed to avoid glitches in the output.
#         if not self.changing_delay:
#             indata = self.delay(indata, self.delay_samples)
#         else:
#             indata = self.delay(indata, self.delay_samples, crossfading=True)
#             self.delay_samples = self.new_delay_samples
#             self.changing_delay = False
#         
#         self.delayline_write(indata)
#         
#         
#         # apply distortion
#         indata = self.distortion(indata)

        indata = self.lowpass(indata)
        
        # repack data into a writeable chunk
        outdata = indata.astype('int16')
        outdata = struct.pack('%dh'%(len(outdata)), *list(outdata))
        return outdata
    
    def lowpass(self, indata):
        outdata, self.sos_conditions = signal.sosfilt(self.sos, indata, zi=self.sos_conditions)
        return outdata
        
    
#     def pitch_shift(self, indata, shift):
#         outdata = self.vec_delay(indata, self.sawtooth)
        
        
#     def vec_delay(self, indata, delays):
#         outdata = [self.delayline_read(
        
        
#     def vec_delay_read(self, read_indices, length):
#         int_indices = np.floor(read_indices)
#         frac_indices = read_indices - int_indices
        
        
    def delayline_read(self, read_idx, length):
        """
        Read past data from the delay line.
        
        Parameters:
        read_idx (int): the index into self.circ_buff to start reading at
        length (int): how much data to read. number of samples * 2
        
        Returns:
        np.array('float64'): the past data
        """
        
        if read_idx % 1 == 0:
            read_idx = int(read_idx)
            # read old data from buffer
            delayed_data = self.circ_buff[read_idx:read_idx + length]
            # see if we need to wrap around and add more data from the beginning
            read_idx += length
            if read_idx >= len(self.circ_buff):
                read_idx -= len(self.circ_buff)
                delayed_data = np.concatenate((delayed_data, self.circ_buff[:read_idx]))
        
        if read_idx % 1 != 0:
            int_part = int(np.floor(read_idx))
            frac_part = read_idx - int_part

            pre_idx = int_part - 1
            post_idx = int_part
            if pre_idx < 0:
                pre_idx = len(self.circ_buff) - 1
            
            pre_chunk1 = self.circ_buff[pre_idx:pre_idx + length]
            pre_front = length - (len(self.circ_buff) - pre_idx)
            delta = 0 if pre_front > 0 else pre_front
            pre_chunk2 = self.circ_buff[delta:pre_front]
            
            pre_chunk = np.concatenate((pre_chunk1, pre_chunk2))
                                        
            post_chunk1 = self.circ_buff[post_idx:post_idx + length]
            post_front = length - (len(self.circ_buff) - post_idx)
            delta = 0 if post_front > 0 else post_front
            post_chunk2 = self.circ_buff[delta:post_front]
                            
            post_chunk = np.concatenate((post_chunk1, post_chunk2))
                
            est_chunk = (pre_chunk - post_chunk) * frac_part + post_chunk
            
            delayed_data = est_chunk
            
        return delayed_data
        
    def delay(self, indata, delay_samples, crossfading=False):
        """
        Add delayed audio to an input signal.
        
        Parameters:
        indata (np.array('float64')): the input data
        crossfading (bool): whether or not the delay time is changing.
                            if so, a crossfade is applied to smooth discontinuities.
                            
        Returns:
        np.array('float64'): the input signal plus delayed audio
        """
        if delay_samples == 0:
            return indata
        
        # make sure read index is in the right spot as delay time may have changed        
        self.delay_read_idx = self.write_idx - delay_samples
        if self.delay_read_idx < 0:
            self.delay_read_idx += len(self.circ_buff)
        
        # read delayed data from buffer
        delayed_data = self.delayline_read(self.delay_read_idx, len(indata))
        
        # add delayed data to input signal
        if not crossfading:
            outdata = indata + delayed_data * self.delay_gain
        # if the delay time was changed this chunk, need to also get past data for new delay time
        else:
            new_read_idx = self.write_idx - self.new_delay_samples
            if new_read_idx < 0:
                new_read_idx += len(self.circ_buff)
                
            new_delayed_data = self.delayline_read(new_read_idx, len(indata))
            
            # fade out the old delay data, fade in the new
            outdata_old = (indata + delayed_data * self.delay_gain) * \
                          np.linspace(1, 0, len(indata))
            outdata_new = (indata + new_delayed_data * self.delay_gain) * \
                          np.linspace(0, 1, len(indata))
            outdata = outdata_old + outdata_new
        
        # increment read pointer, check for overflow
        self.delay_read_idx += len(indata)
        if self.delay_read_idx >= len(self.circ_buff):
            self.delay_read_idx -= len(self.circ_buff)
            
        return outdata
    
    def set_delay_samples(self, new_delay):
        """
        Setter for changing the delay time. Lets process() know things are changing.
        
        Parameters:
        new_delay (int): the new delay time, in samples 
        """
        
        self.new_delay_samples = new_delay
        self.changing_delay = True
        
    def delayline_write(self, indata):
        """
        Write data to the delay line.
        
        Parameters:
        write_idx (int): the index into self.circ_buff to start writing at
        indata (np.array('float64')): the data to write
        """
        # if all the data we need is before the end of the buffer
        if self.write_idx + len(indata) < len(self.circ_buff):            
            self.circ_buff[self.write_idx:self.write_idx + len(indata)] = indata
        # otherwise, wrap around and put whatever's left at the beginning
        else:
            space_remaining = len(self.circ_buff) - self.write_idx
            self.circ_buff[self.write_idx:] = indata[:space_remaining]
            self.circ_buff[:len(indata) - space_remaining] = indata[space_remaining:]
            
        self.write_idx += len(indata)
        if self.write_idx >= len(self.circ_buff):
            self.write_idx -= len(self.circ_buff)
        
    def distortion(self, indata):
        """
        Apply wave shaping distortion to the input buffer.

        Parameters:
        indata (np.array('float64')): the audio data to distort
        
        Returns:
        np.array('float64'): the distorted audio
        """
        
        # normalize input data to between [-1, 1]
        indata /= np.iinfo(np.int16).max

        #amount = 0.7
        amount = self.distortion_amount
        k = 2 * amount / (1 - amount)

        # function to apply wave shaping distortion
        distort_sample = lambda samp : (1 + k) * (samp) / (1 + k * abs(samp))

        # run function over input array
        outdata = distort_sample(indata)

        # return data to its original scale
        return outdata * np.iinfo(np.int16).max
    
    def read_sensors(self):
        """
        Continuously poll the ultrasonic sensors for their current readings,
        apply the results to the appropriate effect parameters.
        
        Should run in a separate thread from audio processing.
        """
        while True:
            distortion_dist = round(self.distortion_sensor.distance, 2)
            delay_dist = round(self.delay_sensor.distance, 2)
            
            # map scaled sensor data to effect parameter values, if it has changed
            if distortion_dist != self.distortion_dist:
                self.distortion_amount = 1 - self.linear_scale(distortion_dist,
                                                               self.DISTORTION_MIN,
                                                               self.DISTORTION_MAX)
                self.distortion_dist = distortion_dist
                
            # only detect movements larger than a certain threshold for delay
            if abs(delay_dist - self.delay_dist) > self.delay_dist_tolerance:
                # shut the delay off when distance is large enough
                if delay_dist >= self.MAX_DIST:
                    self.set_delay_samples(0)
                else:
                    self.set_delay_samples((self.linear_scale(delay_dist,
                                                self.DELAY_SECONDS_MIN,
                                                self.adjusted_sample_rate * self.DELAY_SECONDS_MAX)))
                    
                self.delay_dist = delay_dist
                    
            #print(distortion_dist, delay_dist)
            time.sleep(0.01)
            
    def linear_scale(self, distance, param_min, param_max):
        """
        Helper function to scale a distance value to a relevant effect parameter range.
        
        Parameters:
        distance (float): the latest value read from a sensor
        param_min (number): the minimum possible value of the parameter
        param_max (number): the maximum possible value of the parameter
        
        Returns:
        (number): the scaled value
        """
        min_dist = 0
        max_dist = self.MAX_DIST
        percent = (distance - min_dist) / (max_dist - min_dist)
        value = percent * (param_max - param_min) + param_min
        return value

try:
    tester = UltrasonicMusic(44100, 128)
    tester.setup()
    while True:
        input_block = tester.input_device.read()
        processed_block = tester.process(input_block)
        tester.output_device.write(processed_block)

#     plt.plot(range(len(tester.sawtooth)), tester.sawtooth)
#     plt.show()


#     sine = np.sin(np.linspace(0, 2*np.pi - np.pi / 50, 100))
#     tester.circ_buff=sine.copy()
#     delayed_samples = tester.delayline_read(0.9, 61)
#     plt.plot(delayed_samples)
#     plt.plot(sine)
#     plt.show()
        
#     test_chunk = (np.random.rand(128) * np.iinfo(np.int16).max).astype('int16')
#     test_start = time.time()
#     for i in range(500):
#         #outdata = [tester.single_delay(samp) for samp in test_chunk]
#         #outdata = tester.vfunc_single_delay(test_chunk)
#         outdata = tester.delay(test_chunk)
#     test_end = time.time()
#     print('took', test_end - test_start, 'seconds')
except KeyboardInterrupt:
    print('interrupted')
    del tester
    
    
# def single_delay(self, insample):
#     self.circ_buff[self.write_idx] = insample
#     self.write_idx += 1
#     if self.write_idx >= len(self.circ_buff):
#         self.write_idx = 0
#         
#     delayed_sample = self.circ_buff[self.delay_read_idx]
#     self.delay_read_idx += 1
#     if self.delay_read_idx >= len(self.circ_buff):
#         self.delay_read_idx = 0
#         
#     return insample + self.delay_gain * delayed_sample
