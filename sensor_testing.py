from gpiozero import DistanceSensor
import time

sensor1 = DistanceSensor(trigger=17, echo=27, max_distance=0.35, queue_len=5)
sensor2 = DistanceSensor(trigger=23, echo=24, max_distance=0.35, queue_len=5)

class Testes:
    def __init__(self):
        self.var = 0.0
        self.var_max = 88200
        self.tolerance = 1000
        self.MAX_DIST = 0.35
        self.dist = 0
        self.dist_tolerance = 0.02
        
    def linear_scale(self, distance, param_min, param_max):
        min_dist = 0
        max_dist = self.MAX_DIST
        percent = (distance - min_dist) / (max_dist - min_dist)
        value = percent * (param_max - param_min) + param_min
        return value
    
    def set_var(self, new_dist):
        if abs(new_dist - self.dist) < self.dist_tolerance:
            return
        self.dist = new_dist
        new_var = test.linear_scale(new_dist, 0, test.var_max)
        if new_var == self.var_max:
            new_var = 0
            #self.var = 0
            print('max dist')
            #return
        while abs(new_var - self.var) > self.tolerance:
            self.var = self.var + self.tolerance if self.var < new_var else self.var - self.tolerance
            time.sleep(0.002)
            print(self.var)
        self.var = new_var
        print('finished ramping')

test = Testes()

while True:
    dist1 = round(sensor1.distance, 2)
    dist2 = round(sensor2.distance, 2)
    #print(sensor1.distance, sensor2.distance)
    
    #print('scaled dist:', scaled_dist)
    test.set_var(dist2)
    time.sleep(0.01)
