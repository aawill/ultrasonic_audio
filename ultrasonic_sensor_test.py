#Libraries
import RPi.GPIO as GPIO
from gpiozero import DistanceSensor
import time

sensor1 = DistanceSensor(trigger=17, echo=27)
sensor2 = DistanceSensor(trigger=23, echo=24)
sensor1.max_distance = 0.35
sensor2.max_distance = 0.35
#sensor.threshold_distance = 0.4

while True:
    print(sensor1.distance, ' ', sensor2.distance)
    time.sleep(0.01)


#  
# #GPIO Mode (BOARD / BCM)
# GPIO.setmode(GPIO.BCM)
#  
# #set GPIO Pins
# GPIO_TRIGGER = 17
# GPIO_ECHO = 27
#  
# #set GPIO direction (IN / OUT)
# GPIO.setup(GPIO_TRIGGER, GPIO.OUT)
# GPIO.setup(GPIO_ECHO, GPIO.IN)
# 
# # allows readings of up to about 43 cm
# timeout = 0.0025
# 
# GPIO.output(GPIO_TRIGGER, GPIO.LOW)
# print('waiting for sensor to settle')
# time.sleep(2)
#  
# def distance():
#     # set Trigger to HIGH
#     GPIO.output(GPIO_TRIGGER, True)
#  
#     # set Trigger after 0.01ms to LOW
#     time.sleep(0.00001)
#     GPIO.output(GPIO_TRIGGER, False)
#  
#     StartTime = time.time()
#     StopTime = time.time()
#  
#     # save StartTime
#     while GPIO.input(GPIO_ECHO) == 0:
#         StartTime = time.time()
#  
#     # save time of arrival
#     while GPIO.input(GPIO_ECHO) == 1 and time.time() - StartTime < timeout:
#         StopTime = time.time()
#  
#     # time difference between start and arrival
#     TimeElapsed = StopTime - StartTime
#     # multiply with the sonic speed (34300 cm/s)
#     # and divide by 2, because there and back
#     distance = round(TimeElapsed * 17150, 2)
#  
#     return distance
#  
# if __name__ == '__main__':
#     try:
#         while True:
#             dist = distance()
#             print ("Measured Distance = %.1f cm" % dist)
#             time.sleep(0.01)
#  
#         # Reset by pressing CTRL + C
#     except KeyboardInterrupt:
#         print("Measurement stopped by User")
#         GPIO.cleanup()
