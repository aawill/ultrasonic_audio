// ultrasonic sensor settings
#define TRIG (23)
#define ECHO (24)
#define NUM_READINGS (5)

// contains data relevant to the ultrasonic sensors
typedef struct {
	float maxDist;
	float readings[NUM_READINGS];
	int readIndex;
	float total;
	float average;
	float lastAverage;
	int timeoutMicros;
}
sensorData;

void sensorSetup(sensorData *sensor) {
	sensor->maxDist = 31;
	for (int i = 0; i < NUM_READINGS; i++) {
		sensor->readings[i] = sensor->maxDist;
	}
	sensor->readIndex = 0;
	sensor->total = sensor->maxDist * NUM_READINGS;
	sensor->average = 0;
	sensor->lastAverage = 0;
	
	// allows readings up to ~31cm
	sensor->timeoutMicros = 2000;
	

	// makes sure pigpio doesn't hog the audio peripheral we need
	gpioCfgClock(5, 0, 0);
	
	gpioInitialise();
	gpioSetMode(TRIG, PI_OUTPUT);
	gpioSetMode(ECHO, PI_INPUT);

	gpioWrite(TRIG, PI_OFF);
	gpioDelay(30000);
	printf("finished setting up sensor stuff\n");
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

int getAvgValue(sensorData *sensor, float newDist) {
	// subtract the last reading:
	sensor->total -= sensor->readings[sensor->readIndex];
	sensor->readings[sensor->readIndex] = newDist;
	sensor->total += sensor->readings[sensor->readIndex];
	sensor->readIndex++;
	if (sensor->readIndex >= NUM_READINGS) {
		sensor->readIndex = 0;
	}
	// calculate the average:
	sensor->average = sensor->total / NUM_READINGS;
	sensor->lastAverage = sensor->average;
	return sensor->average;
}
