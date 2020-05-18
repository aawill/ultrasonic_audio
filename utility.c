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

float logScale(float distance, float minDist, float maxDist, float paramMin, float paramMax) {
	paramMin = log(paramMin);
	paramMax = log(paramMax);
	float scale = (paramMax - paramMin) / (maxDist - minDist);
	return exp(paramMin + scale * (distance - minDist));
}

void queryDevices() {
	int numDevices;
	numDevices = Pa_GetDeviceCount();
	if (numDevices < 0) {
		printf("ERROR: Pa_CountDevices returned 0x%x\n", numDevices);
	}

	const PaDeviceInfo *deviceInfo;
	for (unsigned int i = 0; i < numDevices; i++) {
		deviceInfo = Pa_GetDeviceInfo( i );
		printf("audio device: %s \n", deviceInfo->name);
	}
}

bool intInArray(int* arr, int size, int val) {
	for (unsigned int i = 0; i < size; ++i) {
		if (arr[i] == val) {
			return true;
		}
	}
	return false;
}

bool floatInArray(float* arr, int size, float val) {
	for (unsigned int i = 0; i < size; ++i) {
		if (arr[i] == val) {
			return true;
		}
	}
	return false;
}

int subRange(float distance, float minDist, float maxDist, int numSubranges) {
	if (distance < minDist || distance > maxDist) {
		return -1;
	}
	//~ int subRangeSize = (maxDist - minDist) / numSubranges;
	for (unsigned int i = 0; i < numSubranges; ++i) {
		if (distance < maxDist * (i+1) / numSubranges) {
			return i;
		}
	}
	return -1;
}
