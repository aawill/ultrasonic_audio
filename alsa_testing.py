import alsaaudio as aa
import numpy as np
import struct
import RPi.GPIO as GPIO
from gpiozero import DistanceSensor
import time
import threading
#import multiprocessing


class UltrasonicMusic:
    def __init__(self, sample_rate, buffer_size):
        self.sample_rate = sample_rate
        self.buffer_size = buffer_size
        
        self.input_device = aa.PCM(type=aa.PCM_CAPTURE, mode=aa.PCM_NONBLOCK, cardindex=0)
        self.input_device.setrate(self.sample_rate)
        self.input_device.setperiodsize(self.buffer_size)
        
        self.output_device = aa.PCM(type=aa.PCM_PLAYBACK, cardindex=0)
        self.output_device.setrate(self.sample_rate)
        self.output_device.setperiodsize(self.buffer_size)

        self.distortion_amount = 0.3
        self.delay_gain = 0.9
        self.delay_time = 0.3

        # circular buffer variables
        self.buff_idx = 1
        self.sample_num = 0
        # stores 4 seconds of audio
        self.circular_buffer = np.zeros(self.sample_rate * 4)
        
        # ultrasonic sensor setup
        GPIO.setmode(GPIO.BCM)
        
        self.sensor1 = DistanceSensor(trigger=17, echo=27)
        self.sensor2 = DistanceSensor(trigger=23, echo=24)
        self.sensor1.max_distance = 0.35
        self.sensor2.max_distance = 0.35
        
        self.MAX_DIST = 0.35
        self.dist1 = self.MAX_DIST
        self.dist2 = self.MAX_DIST
        
        # threading stuff
        self.sensor_thread = threading.Thread(target=self.read_sensors)
        self.sensor_thread.start()
        #self.sensor1_process = multiprocessing.Process(target=self.read_sensors)
        #self.sensor1_process.start()
        
    def __del__(self):
        print('destructing')
        self.sensor_thread.join()
        #self.sensor1_process.join()
        GPIO.cleanup()

    def process(self, read_info):
        frames, inbuffer = read_info
        indata = np.frombuffer(inbuffer, dtype=np.int16)
        indata = np.array(indata, dtype=np.int16)
        
        processed_data = self.distortion(indata)
#         
        outdata = processed_data.astype('int16')
        outdata = struct.pack('%dh'%(len(outdata)), *list(outdata))
        return outdata
    
    def distortion(self, indata):
        # normalize input data to between [-1, 1]
        float_indata = indata.astype('float64')
        float_indata /= np.iinfo(np.int16).max

        amount = 1 - self.linear_scale(self.dist1, 0, self.MAX_DIST, 0.01, 0.99)
        k = 2 * amount / (1 - amount)

        # function to apply wave shaping distortion
        distort_sample = lambda samp : (1 + k) * (samp) / (1 + k * abs(samp))

        # run function over input array
        float_outdata = distort_sample(float_indata)

        # return data to its original scale
        return (float_outdata * np.iinfo(np.int16).max).astype('int16')

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
    
    def read_sensors(self):
        time_start = time.time()
        while time.time() - time_start < 100:
            self.dist1 = round(self.sensor1.distance,  2)
            self.dist2 = round(self.sensor2.distance,  2)
            #print(self.dist1, ' ', self.dist2)
            time.sleep(0.01)
    
    def linear_scale(self, distance, minDist, maxDist, minVal, maxVal):
        percent = (distance - minDist) / (maxDist - minDist)
        value = percent * (maxVal - minVal) + minVal
        return value

try:
    tester = UltrasonicMusic(44100, 128)
    time_start = time.time()
    while time.time() - time_start < 100:
        input_block = tester.input_device.read()
        processed_block = tester.process(input_block)
        tester.output_device.write(processed_block)
except KeyboardInterrupt:
    print('interrupted')
    del tester
    #tester.sensor_thread.join()
    #tester.sensor_process.join()
    #GPIO.cleanup()
    #print('cleaned up')
