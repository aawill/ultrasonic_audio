#include <stdio.h>
#include <pigpio.h>

#define TRIG 23
#define ECHO 24

#define NUM_READINGS 10

int readings[NUM_READINGS];
int readIndex = 0;
int total = 0;
int average = 0;

int lastAverage = -1;
// allows readings up to ~31cm
int timeoutMicros = 2000;

void setup() {
	for (int i = 0; i < NUM_READINGS; i++) {
		readings[i] = 0;
	}

	gpioInitialise();
	gpioSetMode(TRIG, PI_OUTPUT);
	gpioSetMode(ECHO, PI_INPUT);

	gpioWrite(TRIG, PI_OFF);
	gpioDelay(30000);
}
 
int getCM(uint32_t timeout) {
	// Send trigger pulse
	gpioWrite(TRIG, PI_ON);
	gpioDelay(20);
	gpioWrite(TRIG, PI_OFF);

	// Wait for echo start
	while (gpioRead(ECHO) == PI_OFF);

	// Wait for echo end
	uint32_t startTime = gpioTick();
	while (gpioRead(ECHO) == PI_ON) {
		if (gpioTick() - startTime >= timeout) {
			return -1;
		}
	}
	uint32_t travelTime = gpioTick() - startTime;
	// Get distance in cm
	int distance = travelTime / 58;
	return distance;
}
 
int main(void) {
	setup();
	while (1) {
		// subtract the last reading:
		total = total - readings[readIndex];
		readings[readIndex] = getCM(timeoutMicros);
		total = total + readings[readIndex];
		readIndex++;
		if (readIndex >= NUM_READINGS) {
			readIndex = 0;
		}
		// calculate the average:
		average = total / NUM_READINGS;
		if (average != lastAverage) {
			lastAverage = average;
			printf("%d \n", average);
		}
		time_sleep(0.01); 
	}
	return 0;
}
