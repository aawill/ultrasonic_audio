import alsaaudio as aa
import numpy as np
import struct
import RPi.GPIO as GPIO
from gpiozero import DistanceSensor
import time
import threading


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
        self.circular_buffer = np.zeros(self.adjusted_sample_rate * 4)  
        # location of read and write indices into circular buffer
        self.read_idx = 0
        self.write_idx = 0
        
        self.changing_delay = False
                      
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
        GPIO.cleanup()

    def setup(self):
        """Prepare tester to run."""
        
        # preload buffer with four chunks to avoid wacky noises on startup
        preload_data = np.zeros(self.buffer_size * 2 * 4, dtype='int16')
        preload_data = struct.pack('%dh'%(len(preload_data)), *list(preload_data))
        self.output_device.write(preload_data)
        # start reading from sensors
        self.sensor_thread.start()

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

        # apply delay. if the value of delay_samples has been changed this iteration,
        # additional processing is needed to avoid glitches in the output.
        if not self.changing_delay:
            indata = self.delay(indata)
        else:
            indata = self.delay(indata, crossfading=True)
            self.delay_samples = self.new_delay_samples
            self.changing_delay = False

        # apply distortion
        indata = self.distortion(indata)
        
        # repack data into a writeable chunk
        outdata = indata.astype('int16')
        outdata = struct.pack('%dh'%(len(outdata)), *list(outdata))
        return outdata
    
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
    
    def set_delay_samples(self, new_delay):
        """
        Setter for changing the delay time. Lets process() know things are changing.
        
        Parameters:
        new_delay (int): the new delay time, in samples 
        """
        
        self.new_delay_samples = new_delay
        self.changing_delay = True
        
    def delayline_read(self, read_idx, length):
        """
        Read past data from the delay line.
        
        Parameters:
        read_idx (int): the index into self.circular_buffer to start reading at
        length (int): how much data to read. number of samples * 2
        
        Returns:
        np.array('float64'): the past data
        """
        # read old data from buffer
        delayed_data = self.circular_buffer[read_idx:read_idx + length]
        # see if we need to wrap around and add more data from the beginning
        read_idx += length
        if read_idx >= len(self.circular_buffer):
            read_idx -= len(self.circular_buffer)
            delayed_data = np.concatenate((delayed_data, self.circular_buffer[:read_idx]))
        return delayed_data
        
    def delayline_write(self, write_idx, indata):
        """
        Write data to the delay line.
        
        Parameters:
        write_idx (int): the index into self.circular_buffer to start writing at
        indata (np.array('float64')): the data to write
        """
        # if all the data we need is before the end of the buffer
        if write_idx + len(indata) < len(self.circular_buffer):            
            self.circular_buffer[write_idx:write_idx + len(indata)] = indata
        # otherwise, wrap around and put whatever's left at the beginning
        else:
            space_remaining = len(self.circular_buffer) - write_idx
            self.circular_buffer[write_idx:] = indata[:space_remaining]
            self.circular_buffer[:len(indata) - space_remaining] = indata[space_remaining:]
            
    def delay(self, indata, crossfading=False):
        """
        Add delayed audio to an input signal.
        
        Parameters:
        indata (np.array('float64')): the input data
        crossfading (bool): whether or not the delay time is changing.
                            if so, a crossfade is applied to smooth discontinuities.
                            
        Returns:
        np.array('float64'): the input signal plus delayed audio
        """
        if self.delay_samples == 0:
            return indata
        
        # make sure read index is in the right spot as delay time may have changed        
        self.read_idx = self.write_idx - self.delay_samples
        if self.read_idx < 0:
            self.read_idx += len(self.circular_buffer)
        
        # read delayed data from buffer
        delayed_data = self.delayline_read(self.read_idx, len(indata))
        # write new data to buffer, plus old data for feedback
        self.delayline_write(self.write_idx, indata + delayed_data * self.delay_gain)
        
        # add delayed data to input signal
        if not crossfading:
            outdata = indata + delayed_data * self.delay_gain
        # if the delay time was changed this chunk, need to also get past data for new delay time
        else:
            new_read_idx = self.write_idx - self.new_delay_samples
            if new_read_idx < 0:
                new_read_idx += len(self.circular_buffer)
                
            new_delayed_data = self.delayline_read(new_read_idx, len(indata))
            
            # fade out the old delay data, fade in the new
            outdata_old = (indata + delayed_data * self.delay_gain) * np.linspace(1, 0, len(indata))
            outdata_new = (indata + new_delayed_data * self.delay_gain) * np.linspace(0, 1, len(indata))
            outdata = outdata_old + outdata_new
        
        # increment pointers, check for overflow
        self.write_idx += len(indata)
        if self.write_idx >= len(self.circular_buffer):
            self.write_idx -= len(self.circular_buffer)
        self.read_idx += len(indata)
        if self.read_idx >= len(self.circular_buffer):
            self.read_idx -= len(self.circular_buffer)
            
        return outdata
    
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
                    self.set_delay_samples(int(self.linear_scale(delay_dist,
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
#     self.circular_buffer[self.write_idx] = insample
#     self.write_idx += 1
#     if self.write_idx >= len(self.circular_buffer):
#         self.write_idx = 0
#         
#     delayed_sample = self.circular_buffer[self.read_idx]
#     self.read_idx += 1
#     if self.read_idx >= len(self.circular_buffer):
#         self.read_idx = 0
#         
#     return insample + self.delay_gain * delayed_sample
