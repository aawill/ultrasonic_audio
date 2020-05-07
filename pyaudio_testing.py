"""PyAudio Example: Play a wave file."""

import pyaudio
import sys
import time
import numpy as np
from matplotlib import pyplot as plt
import struct

WIDTH = 2
BUFFER_SIZE = 1024
SAMPLE_RATE = 44100
CHANNELS = 1

p = pyaudio.PyAudio()

input_index = -1
#output_index = -1

num_devices = p.get_device_count()
for i in range(num_devices):
  device = p.get_device_info_by_index(i)
  if device['name'] == 'Line In (Scarlett 18i20 USB)':
    input_index = i
  # elif device['name'] == 'Line Out (Scarlett 18i20 USB)':
  #   output_index = i
  #   print("output device:", device, '\n')

if input_index == -1:
  print("target input device not found")
# if output_index == -1:
#   print("target output device not found")

# print("input device index:", input_index)
# print("output device index:", output_index)

stream = p.open(rate=SAMPLE_RATE, channels=CHANNELS,
                   format=pyaudio.paInt16, 
                   input=True, output=True, frames_per_buffer=BUFFER_SIZE,
                   input_device_index=input_index)

# print("input latency", stream.get_input_latency())
# print("output latency", stream.get_output_latency())

data_to_plot = np.array([], dtype=np.int16)

time_start = time.time()
while time.time() - time_start < 10:
  # read samples from input stream
  data = stream.read(BUFFER_SIZE)
  # convert data from bytestring into numpy array
  data = np.frombuffer(data, dtype=np.int16)

  data = np.fft.rfft(data)
  data = np.roll(data, 5)
  data[0:5] = 0
  data = np.fft.irfft(data)

  dataout = np.array(data, dtype='int16')
  chunkout = struct.pack("%dh"%(len(dataout)), *list(dataout)) 

  #data_to_plot = np.append(data_to_plot, numpydata)

  #plt.plot(numpydata)
  #plt.pause(0.005)
  
  # write audio to output
  stream.write(chunkout)

# plot against time in seconds instead of samples
#time_axis = np.arange(len(data_to_plot)) / SAMPLE_RATE

#plt.plot(time_axis, data_to_plot)
#plt.show()

stream.close()
