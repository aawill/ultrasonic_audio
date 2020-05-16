typedef struct {
	float gain;
}
Gain;

Gain* Gain_create(float _gain) {
	Gain* g = (Gain*)malloc(sizeof(Gain));
	g->gain = _gain;
	return g;
}

void Gain_destroy(Gain* g) {
	free(g);
}

void Gain_set(Gain* g, float newGain) {
	g->gain = newGain;
}
float Gain_get(Gain* g) {
	return g->gain;
}

float Gain_apply(Gain* g, float sample) {
	return sample * g->gain;
}

typedef struct {
	float amount;
}
Distortion;

Distortion* Distortion_create(float _amount) {
	Distortion* dist = (Distortion*)malloc(sizeof(Distortion));
	dist->amount = _amount;
	return dist;
}

void Distortion_destroy(Distortion* dist) {
	free(dist);
}

void Distortion_set(Distortion* dist, float newAmount) {
	dist->amount = newAmount;
}
float Distortion_get(Distortion* dist) {
	return dist->amount;
}

float Distortion_apply(Distortion* d, float sample) {
	float k = 2 * d->amount / (1 - d->amount);
	return (1 + k) * sample / (1 + k * abs(sample));
}

typedef struct {
	int delaySamps;
	float feedback;
	int buffSize;
	int chunkSize;
	
	float* buffer;
	float* readPtr;
	float* writePtr;
	
	float* newReadPtr;
	int newDelaySamps;
	bool changingDelay;
	float crossfadeFactor;
}
Delay;

Delay* Delay_create(int _delaySamps, float _feedback, int _buffSize, int _chunkSize) {
	Delay* del = (Delay*)malloc(sizeof(Delay));
	del->delaySamps = _delaySamps;
	del->feedback = _feedback;
	del->buffSize = _buffSize;
	del->chunkSize = _chunkSize;
	
	del->buffer = (float*)malloc(sizeof(float) * del->buffSize);
	// initialize circular buffer with zeros
	for (unsigned int i = 0; i < del->buffSize; ++i) {
		del->buffer[i] = 0;
	}
	// start write at the beginning
	del->writePtr = del->buffer;
	// read should be delaySamps samples before write
	del->readPtr = del->writePtr - del->delaySamps;
	// bounds check
	if (del->readPtr < del->buffer) {
		del->readPtr += del->buffSize;
	}
	del->newReadPtr = del->readPtr;
	del->newDelaySamps = del->delaySamps;
	del->changingDelay = false;
	del->crossfadeFactor = 0;

	return del;
}

void Delay_destroy(Delay* del) {
	free(del->buffer);
	free(del);
}

void Delay_setTime(Delay* del, int _delaySamps) {
	del->newDelaySamps = _delaySamps;
	del->changingDelay = true;
}

void Delay_setFeedback(Delay* del, float _feedback) {
	del->feedback = _feedback;
}

float Delay_apply(Delay* del, float sample) {
	// newDelaySamps will be the same as delaySamps unless delay is changing
	if (del->newDelaySamps == 0 && !del->changingDelay) {
		return sample;
	}
	// calculate read pointer position, bounds check
	del->readPtr = del->writePtr - del->delaySamps;
	if (del->readPtr < del->buffer) {
		del->readPtr += del->buffSize;
	}
	
	if (del->changingDelay) {
		// calculate new read pointer position, bounds check
		del->newReadPtr = del->writePtr - del->newDelaySamps;
		if (del->newReadPtr < del->buffer) {
			del->newReadPtr += del->buffSize;
		}
		// need past data from two different points, crossfade between them
		float oldData = sample + del->feedback * (*del->readPtr);
		float newData = sample + del->feedback * (*del->newReadPtr);
		sample = newData * del->crossfadeFactor + oldData * (1 - del->crossfadeFactor);
		// should complete the crossfade in one chunk
		del->crossfadeFactor += 1.0f / del->chunkSize;
		// indicates we're done crossfading
		if (del->crossfadeFactor >= 1) {
			del->crossfadeFactor = 0;
			del->changingDelay = false;
			del->readPtr = del->newReadPtr;
			del->delaySamps = del->newDelaySamps;
		}
	}
	else {	
		// add past data from delay line
		sample += del->feedback * (*del->readPtr);
	}
	// write to delay line, increment write pointer
	*del->writePtr++ = sample;
	// bounds check
	if (del->writePtr >= del->buffer + del->buffSize) {
		del->writePtr -= del->buffSize;
	}
	
	return sample;
}

typedef struct {
	Gain* gain;
	Distortion* distortion;
	Delay* delay;
}
Effects;

Effects* Effects_create(Gain* _gain, Distortion* _dist, Delay* _del) {
	Effects* fx = (Effects*)malloc(sizeof(Effects));
	fx->gain = _gain;
	fx->distortion = _dist;
	fx->delay = _del;
	return fx;
}

void Effects_destroy(Effects* fx) {
	Gain_destroy(fx->gain);
	Distortion_destroy(fx->distortion);
	Delay_destroy(fx->delay);
}

