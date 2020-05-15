// contains data relevant to the ultrasonic sensors
typedef struct {
	int trigPin;
	int echoPin;
	float maxDist;
	int numReadings;

	int timeoutMicros;
	float* readings;
	float total;
	int readIndex;	
	float average;
	float lastAverage;
}
Sensor;

Sensor* Sensor_create(int _trigPin, int _echoPin, float _maxDist, int _numReadings) {
	Sensor* sensor = (Sensor*)malloc(sizeof(Sensor));
	sensor->trigPin = _trigPin;
	sensor->echoPin = _echoPin;
	sensor->maxDist = _maxDist;
	sensor->numReadings = _numReadings;

	// converts cm to microseconds
	sensor->timeoutMicros = sensor->maxDist * 58;
	sensor->readings = (float*)malloc(sizeof(float) * sensor->numReadings);
	for (int i = 0; i < sensor->numReadings; i++) {
		sensor->readings[i] = sensor->maxDist;
	}
	sensor->total = sensor->maxDist * sensor->numReadings;
	sensor->readIndex = 0;
	sensor->average = 0;
	sensor->lastAverage = 0;

	gpioSetMode(sensor->trigPin, PI_OUTPUT);
	gpioSetMode(sensor->echoPin, PI_INPUT);

	gpioWrite(sensor->trigPin, PI_OFF);

	return sensor;
}

void Sensor_destroy(Sensor* sensor) {
	free(sensor->readings);
	free(sensor);
}

int Sensor_getCM(Sensor* sensor) {
	// Send trigger pulse
	gpioWrite(sensor->trigPin, PI_ON);
	gpioDelay(20);
	gpioWrite(sensor->trigPin, PI_OFF);

	// Wait for echo start
	while (gpioRead(sensor->echoPin) == PI_OFF);

	// Wait for echo end
	uint32_t startTime = gpioTick();
	while (gpioRead(sensor->echoPin) == PI_ON) {
		if (gpioTick() - startTime >= sensor->timeoutMicros) {
			return -1;
		}
	}
	uint32_t travelTime = gpioTick() - startTime;
	// Get distance in cm
	int distance = travelTime / 58;
	return distance;
}

int Sensor_getAvgValue(Sensor* sensor, float newDist) {
	// subtract the last reading:
	sensor->total -= sensor->readings[sensor->readIndex];
	sensor->readings[sensor->readIndex] = newDist;
	sensor->total += sensor->readings[sensor->readIndex];
	sensor->readIndex++;
	if (sensor->readIndex >= sensor->numReadings) {
		sensor->readIndex = 0;
	}
	// calculate the average:
	sensor->average = sensor->total / sensor->numReadings;
	sensor->lastAverage = sensor->average;
	return sensor->average;
}
