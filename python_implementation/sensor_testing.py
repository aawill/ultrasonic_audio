from gpiozero import DistanceSensor
import time

sensor1 = DistanceSensor(trigger=17, echo=27, max_distance=0.35, queue_len=5)
sensor2 = DistanceSensor(trigger=23, echo=24, max_distance=0.35, queue_len=5)
sensor3 = DistanceSensor(trigger=5, echo=6, max_distance=0.35, queue_len=5)

while True:
    dist1 = round(sensor1.distance, 2)
    dist2 = round(sensor2.distance, 2)
    dist3 = round(sensor3.distance, 2)
    print(dist1, dist2, dist3)
    
    time.sleep(0.01)
