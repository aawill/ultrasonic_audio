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
	int sampleRate;
	int chunkSize;

	int buffSize;
	float* buffer;
	float* readPtr;
	float* writePtr;

	int newDelaySamps;
	bool changingDelay;
	float crossfadeFactor;
}
Delay;

Delay* Delay_create(int _delaySamps, float _feedback, int _sampleRate, int _chunkSize) {
	Delay* del = (Delay*)malloc(sizeof(Delay));
	del->delaySamps = _delaySamps;
	del->feedback = _feedback;
	del->sampleRate = _sampleRate;
	del->chunkSize = _chunkSize;

	del->buffSize = del->sampleRate * 4;
	del->buffer = (float*)malloc(sizeof(float) * del->buffSize);
	// initialize circular buffer with zeros
	for (unsigned int i = 0; i < del->buffSize; ++i) {
		del->buffer[i] = 0;
	}
	// start write at the beginning
	del->writePtr = del->buffer;

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
		float* newReadPtr = del->writePtr - del->newDelaySamps;
		if (newReadPtr < del->buffer) {
			newReadPtr += del->buffSize;
		}
		// need past data from two different points, crossfade between them
		float oldData = sample + del->feedback * (*del->readPtr);
		float newData = sample + del->feedback * (*newReadPtr);
		sample = newData * del->crossfadeFactor + oldData * (1 - del->crossfadeFactor);
		// should complete the crossfade in one chunk
		del->crossfadeFactor += 1.0f / del->chunkSize;
		// indicates we're done crossfading
		if (del->crossfadeFactor >= 1) {
			del->crossfadeFactor = 0;
			del->changingDelay = false;
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

/*
 * EFFECT: FRACTIONAL DELAY
 * Operates on the same principles as the regular delay.
 * Allows for fractional delay times, or reading 'between samples'.
 * Does not crossfade delay time changes.
 * Useful for time-based effects such as flanging and pitch shifting.
 */
typedef struct {
	float delaySamps;
	int sampleRate;

	int buffSize;
	float* buffer;
	float* readPtr;
	float* writePtr;

	float crossfadeFactor;
	bool fading;
	int newDelaySamps;
}
FracDelay;

FracDelay* FracDelay_create(float _delaySamps, int _sampleRate) {
	FracDelay* del = (FracDelay*)malloc(sizeof(FracDelay));
	del->delaySamps = _delaySamps;
	del->sampleRate = _sampleRate;
	del->buffSize = del->sampleRate * 2;

	del->buffer = (float*)malloc(sizeof(float) * del->buffSize);
	for (unsigned int i = 0; i < del->buffSize; ++i) {
		del->buffer[i] = 0;
	}
	del->writePtr = del->buffer;
	del->crossfadeFactor = 0;
	del->fading = false;
	return del;
}

void FracDelay_destroy(FracDelay* del) {
	free(del->buffer);
	free(del);
}

void FracDelay_setTime(FracDelay* del, float _delaySamps) {
	del->delaySamps = _delaySamps;
	//~ printf("setting delay to %f\n", _delaySamps);
}

float FracDelay_apply(FracDelay* del, float sample) {
	if (del->delaySamps == 0) {
		return sample;
	}
	// write to delay line, increment write pointer
	*del->writePtr++ = sample;
	// bounds check
	if (del->writePtr >= del->buffer + del->buffSize) {
		del->writePtr -= del->buffSize;
	}
	// split delay time into integer and fractional parts
	int intDelay = (int)floor(del->delaySamps);
	float fracDelay = del->delaySamps - intDelay;
	// calculate read pointer position, bounds check
	del->readPtr = del->writePtr - intDelay;
	if (del->readPtr < del->buffer) {
		del->readPtr += del->buffSize;
	}
	// interpolate between two samples nearest to our fractional delay time
	float* y0 = del->readPtr - 1;
	float* y1 = del->readPtr;
	if (y0 < del->buffer) {
		y0 += del->buffSize;
	}

	if (del->fading) {
		// split delay time into integer and fractional parts
		int intDelayNew = (int)floor(del->newDelaySamps);
		float fracDelayNew = del->newDelaySamps - intDelayNew;
		// calculate new read pointer position, bounds check
		float* newReadPtr = del->writePtr - intDelayNew;
		if (newReadPtr < del->buffer) {
			newReadPtr += del->buffSize;
		}

		float* y0_new = newReadPtr - 1;
		float* y1_new = newReadPtr;
		if (y0_new < del->buffer) {
			y0_new += del->buffSize;
		}
		
		// need past data from two different points, crossfade between them
		float oldData = sample = (*y0 - *y1) * fracDelay + *y1;
		float newData = sample = (*y0_new - *y1_new) * fracDelayNew + *y1_new;
		
		sample = newData * del->crossfadeFactor + oldData * (1 - del->crossfadeFactor);
		// should complete the crossfade in one chunk
		del->crossfadeFactor += 1.0f / 128;
		
		// indicates we're done crossfading
		if (del->crossfadeFactor >= 1) {
			del->crossfadeFactor = 0;
			del->fading = false;
			del->delaySamps = del->newDelaySamps;
		}
	}
	else {
		// add past data from delay line
		sample = (*y0 - *y1) * fracDelay + *y1;
	}
	
	

	return sample;
}

typedef struct {
	float semitones;
	int sampleRate;

	float shiftFactor;
	float maxDelaySamps;
	FracDelay* delay1;
	FracDelay* delay2;
	float t;
}
PShift;

PShift* PShift_create(float _semitones, float _sampleRate) {
	PShift* pshift = (PShift*)malloc(sizeof(PShift));
	pshift->semitones = _semitones;
	pshift->sampleRate = _sampleRate;

	//~ float root = pow(2, pshift->semitones / 12);
	pshift->shiftFactor = 0.2;

	// delay will modulate between 0-100 ms
	pshift->maxDelaySamps = pshift->sampleRate / 10;

	// create two delay lines, 180 degrees out of phase with each other
	pshift->delay1 = FracDelay_create(0, pshift->sampleRate);
	// (second delay line starts halfway up the ramp)
	pshift->delay2 = FracDelay_create(pshift->maxDelaySamps / 2, pshift->sampleRate);

	pshift->t = 0;
	return pshift;
}

void PShift_destroy(PShift* pshift) {
	FracDelay_destroy(pshift->delay1);
	FracDelay_destroy(pshift->delay2);
	free(pshift);
}

void PShift_set(PShift* pshift, float _shiftFactor) {
	pshift->shiftFactor = _shiftFactor;
}

float PShift_apply(PShift* pshift, float sample) {
	// apply delay, then modulate the delay time
	float sample1 = FracDelay_apply(pshift->delay1, sample);
	float sample2 = FracDelay_apply(pshift->delay2, sample);
	// subtract so that positive shift amounts create upward shift - delay ramps down
	float newTime1 = pshift->delay1->delaySamps - pshift->shiftFactor;
	float newTime2 = pshift->delay2->delaySamps - pshift->shiftFactor;
	// bounds checking creates the sawtooth shape
	if (newTime1 <= 0) {
		newTime1 = pshift->maxDelaySamps;
	}
	else if (newTime1 >= pshift->maxDelaySamps) {
		newTime1 = pshift->maxDelaySamps;
	}
	if (newTime2 <= 0) {
		newTime2 = pshift->maxDelaySamps;
	}
	else if (newTime2 >= pshift->maxDelaySamps) {
		newTime2 = pshift->maxDelaySamps;
	}

	FracDelay_setTime(pshift->delay1, newTime1);
	FracDelay_setTime(pshift->delay2, newTime2);

	// create cosine windows to mask discontinuities in delay ramping
	float rampFreq = 5 * pshift->shiftFactor * 2*M_PI;
	//~ printf("%f\n", rampFreq);
	float ramp1 = fabs(cos(rampFreq * pshift->t + M_PI_2));
	float ramp2 = 1 - ramp1;

	// increment our time axis by 1/sampleRate, wrap at 1
	pshift->t = fmod(pshift->t + (1.0 / pshift->sampleRate), 1.0f);
	
	// crossfade between windowed signals
	//~ return sample1 * ramp1;
	//~ return sample2 * ramp2;
	return sample1 * ramp1 + sample2 * ramp2;
}



typedef struct {
	Gain* gain;
	Distortion* distortion;
	Delay* delay;
	PShift* pshift;
}
Effects;

Effects* Effects_create(Gain* _gain, Distortion* _distortion, Delay* _delay, PShift* _pshift) {
	Effects* fx = (Effects*)malloc(sizeof(Effects));
	fx->gain = _gain;
	fx->distortion = _distortion;
	fx->delay = _delay;
	fx->pshift = _pshift;
	return fx;
}

void Effects_destroy(Effects* fx) {
	Gain_destroy(fx->gain);
	Distortion_destroy(fx->distortion);
	Delay_destroy(fx->delay);
	PShift_destroy(fx->pshift);
}

