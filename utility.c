float linearScale(float distance, float minDist, float maxDist, float paramMin, float paramMax) {
	/*
	Helper function to scale a distance value to a relevant effect parameter range.
	
	Parameters:
	distance (float): the latest value read from a sensor
	param_min (number): the minimum possible value of the parameter
	param_max (number): the maximum possible value of the parameter
	
	Returns:
	(number): the scaled value
	*/
	float percent = (distance - minDist) / (maxDist - minDist);
	float value = percent * (paramMax - paramMin) + paramMin;
	return value;
}

void queryDevices() {
	PaError err;
	int numDevices;
	numDevices = Pa_GetDeviceCount();
	if (numDevices < 0) {
		printf("ERROR: Pa_CountDevices returned 0x%x\n", numDevices);
		err = numDevices;
	}

	const PaDeviceInfo *deviceInfo;
	for (unsigned int i = 0; i < numDevices; i++) {
		deviceInfo = Pa_GetDeviceInfo( i );
		printf("audio device: %s \n", deviceInfo->name);
	}
}
