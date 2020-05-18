typedef struct {
	float gain;
	bool active;
}
Gain;

Gain* Gain_create(float _gain) {
	Gain* g = (Gain*)malloc(sizeof(Gain));
	g->gain = _gain;
	g->active = true;
	return g;
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

void Gain_destroy(Gain* g) {
	free(g);
}

typedef struct {
	float amount;
	
	bool active;
}
Distortion;

Distortion* Distortion_create(float _amount) {
	Distortion* dist = (Distortion*)malloc(sizeof(Distortion));
	dist->amount = _amount;
	dist->active = true;
	return dist;
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

void Distortion_destroy(Distortion* dist) {
	free(dist);
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

	bool active;
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
	del->active = true;

	return del;
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
	// if we need to crossfade between old and new delay times
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

void Delay_destroy(Delay* del) {
	free(del->buffer);
	free(del);
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

	bool active;
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
	del->active = true;
	return del;
}

void FracDelay_setTime(FracDelay* del, float _delaySamps) {
	del->delaySamps = _delaySamps;
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

	// add past data from delay line
	sample = (*y0 - *y1) * fracDelay + *y1;
	
	return sample;
}

void FracDelay_destroy(FracDelay* del) {
	free(del->buffer);
	free(del);
}

typedef struct {
	float semitones;
	int sampleRate;

	float shiftFactor;
	float maxDelay;
	FracDelay* delay1;
	FracDelay* delay2;
	
	float fade1;
	bool fadingDown1;
	bool fadingUp1;
	float fade2;
	bool fadingDown2;
	bool fadingUp2;
	float tolerance;
	float fadeOffset;

	bool globalFadeUp;
	bool globalFadeDown;
	float effectGain;
	float newEffectGain;
	bool active;
}
PShift;

PShift* PShift_create(float _semitones, float _sampleRate) {
	PShift* pshift = (PShift*)malloc(sizeof(PShift));
	pshift->semitones = _semitones;
	pshift->sampleRate = _sampleRate;
	
	// controls the speed of the sawtooth delay time ramp, and therefore output pitch
	pshift->shiftFactor = pow(2, pshift->semitones / 12) - 1;
	// delay will modulate between 0-100 ms
	pshift->maxDelay = pshift->sampleRate / 10;
	// create two delay lines, 180 degrees out of phase with each other
	pshift->delay1 = FracDelay_create(0, pshift->sampleRate);
	// (second delay line starts halfway up the ramp)
	pshift->delay2 = FracDelay_create(pshift->maxDelay / 2, pshift->sampleRate);
	// variables to handle crossfading between delay lines
	pshift->fade1 = 0;
	pshift->fadingDown1 = false;
	pshift->fadingUp1 = false;
	pshift->fade2 = 0;
	pshift->fadingDown2 = false;
	pshift->fadingUp2 = false;
	// help detect when to start crossfading
	pshift->tolerance = fabs(pshift->shiftFactor / 2);
	pshift->fadeOffset = pshift->shiftFactor * 1000;

	// vars to handle ramping an entire PShift voice's gain 
	pshift->globalFadeUp = false;
	pshift->globalFadeDown = false;
	pshift->effectGain = 1;
	pshift->newEffectGain = 1;
	pshift->active = true;
	return pshift;
}

void PShift_setGain(PShift* pshift, float newGain) {
	// reject gain changes while we're already ramping 
	if (pshift->globalFadeDown || pshift->globalFadeUp) {
		return;
	}
	if (newGain < pshift->effectGain) {
		pshift->globalFadeDown = true;
	}
	else if (newGain > pshift->effectGain) {
		pshift->globalFadeUp = true;
	}
	pshift->newEffectGain = newGain;
}

void PShift_set(PShift* pshift, float _shiftFactor) {
	pshift->shiftFactor = _shiftFactor;
}

float PShift_apply(PShift* pshift, float sample) {
	if (pshift->semitones == 0) {
		return sample;
	}
	// apply current delays
	float sample1 = FracDelay_apply(pshift->delay1, sample);
	float sample2 = FracDelay_apply(pshift->delay2, sample);
	// calculate new delay times
	float newTime1 = pshift->delay1->delaySamps - pshift->shiftFactor;
	float newTime2 = pshift->delay2->delaySamps - pshift->shiftFactor;
	// need to wrap around 0 and maxDelay to create sawtooth shape
	// (add maxDelay so negative delays wrap right)
	newTime1 = fmod(newTime1 + pshift->maxDelay, pshift->maxDelay);
	newTime2 = fmod(newTime2 + pshift->maxDelay, pshift->maxDelay);
	// determine when to start fading down+up to avoid discontinuity
	float fadeSample = fmod(pshift->maxDelay + pshift->fadeOffset, pshift->maxDelay);
	// probably won't hit fadeSample exactly, so detect when we're close enough
	if (fabs(newTime1 - fadeSample) < pshift->tolerance) {
		pshift->fadingDown1 = true;
		pshift->fadingUp2 = true;
	}
	if (fabs(newTime2 - fadeSample) < pshift->tolerance) {
		pshift->fadingDown2 = true;
		pshift->fadingUp1 = true;
	}
	// set new delay times
	FracDelay_setTime(pshift->delay1, newTime1);
	FracDelay_setTime(pshift->delay2, newTime2);
	// apply crossfading between delay lines if necessary
	if (pshift->fadingDown1) {
		pshift->fade1 -= 0.001;
		if (pshift->fade1 <= 0) {
			pshift->fade1 = 0;
			pshift->fadingDown1 = false;
		}
	}
	else if (pshift->fadingUp1) {
		pshift->fade1 += 0.001;
		if (pshift->fade1 >= 1) {
			pshift->fade1 = 1;
			pshift->fadingUp1 = false;
		}
	}
	if (pshift->fadingDown2) {
		pshift->fade2 -= 0.001;
		if (pshift->fade2 <= 0) {
			pshift->fade2 = 0;
			pshift->fadingDown2 = false;
		}
	}
	else if (pshift->fadingUp2) {
		pshift->fade2 += 0.001;
		if (pshift->fade2 >= 1) {
			pshift->fade2 = 1;
			pshift->fadingUp2 = false;
		}
	}
	sample1 *= pshift->fade1;
	sample2 *= pshift->fade2;
	float output = (sample1 + sample2) * pshift->effectGain;

	// apply any fading up or down of the effect gain that may be occurring
	if (pshift->globalFadeDown) {
		pshift->effectGain -= 0.001;
		if (pshift->effectGain <= pshift->newEffectGain) {
			pshift->effectGain = pshift->newEffectGain;
			pshift->globalFadeDown = false;
		}
	}
	else if (pshift->globalFadeUp) {
		pshift->effectGain += 0.001;
		if (pshift->effectGain >= pshift->newEffectGain) {
			pshift->effectGain = pshift->newEffectGain;
			pshift->globalFadeUp = false;
		}
	} 
	return output;
}

void PShift_destroy(PShift* pshift) {
	FracDelay_destroy(pshift->delay1);
	FracDelay_destroy(pshift->delay2);
	free(pshift);
}

typedef struct {
	int numVoices;
	int* shiftAmounts;
	float* mixAmounts;
	int sampleRate;
	
	PShift** shifters;
	bool* activeVoices;

	bool active;
}
Harmonizer;

Harmonizer* Harmonizer_create(int _numVoices, int* _shiftAmounts, float* _mixAmounts, int _sampleRate) {
	Harmonizer* harm = (Harmonizer*)malloc(sizeof(Harmonizer));
	harm->numVoices = _numVoices;
	harm->shiftAmounts = (int*)malloc(sizeof(int) * harm->numVoices);
	harm->mixAmounts = (float*)malloc(sizeof(float) * harm->numVoices);
	harm->shifters = (PShift**)malloc(sizeof(PShift*) * harm->numVoices);
	harm->activeVoices = (bool*)malloc(sizeof(bool) * harm->numVoices);
	harm->sampleRate = _sampleRate;
	for (unsigned int i = 0; i < harm->numVoices; ++i) {
		harm->shiftAmounts[i] = _shiftAmounts[i];
		harm->mixAmounts[i] = _mixAmounts[i];
		harm->shifters[i] = PShift_create(harm->shiftAmounts[i], harm->sampleRate);
		harm->activeVoices[i] = true;
	}
	harm->active = true;
	return harm;
}

void Harmonizer_addVoice(Harmonizer* harm, int shift, float mix) {
	// grow and update internal arrays
	harm->shiftAmounts = (int*)realloc(harm->shiftAmounts, sizeof(int) * ++harm->numVoices);
	harm->mixAmounts = (float*)realloc(harm->mixAmounts, sizeof(float) * harm->numVoices);
	harm->shifters = (PShift**)realloc(harm->shifters, sizeof(PShift*) * harm->numVoices);
	harm->activeVoices = (bool*)realloc(harm->activeVoices, sizeof(bool) * harm->numVoices);
	harm->shiftAmounts[harm->numVoices - 1] = shift;
	harm->mixAmounts[harm->numVoices - 1] = mix;
	harm->shifters[harm->numVoices - 1] = PShift_create(shift, harm->sampleRate);
	harm->activeVoices[harm->numVoices - 1] = true;
}

void Harmonizer_enableVoice(Harmonizer* harm, int voice) {
	harm->activeVoices[voice] = true;
	//~ PShift_setGain(harm->shifters[voice], 1);
}

void Harmonizer_disableVoice(Harmonizer* harm, int voice) {
	//~ harm->activeVoices[voice] = false;
	PShift_setGain(harm->shifters[voice], 0);
}

void Harmonizer_setActiveVoices(Harmonizer* harm, int numActive) {
	unsigned int i = 0;
	// enable active voices
	for (i = 0; i < numActive; ++i) {
		Harmonizer_enableVoice(harm, i);
	}
	// disable all other voices
	for (i = numActive; i < harm->numVoices; ++i) {
		Harmonizer_disableVoice(harm, i);
	}
}

void Harmonizer_setVoiceGain(Harmonizer* harm, int voice, float gain) {
	PShift_setGain(harm->shifters[voice], gain);
	//~ harm->mixAmounts[voice] = gain;
}

float Harmonizer_apply(Harmonizer* harm, float sample) {
	float harmSamp = sample;
	for (unsigned int i = 0; i < harm->numVoices; ++i) {
		if (harm->activeVoices[i]) {
			harmSamp += PShift_apply(harm->shifters[i], sample) * harm->mixAmounts[i];
		}
	}
	return harmSamp;
}

void Harmonizer_destroy(Harmonizer* harm) {
	free(harm->shiftAmounts);
	free(harm->mixAmounts);
	for (unsigned int i = 0; i < harm->numVoices; ++i) {
		PShift_destroy(harm->shifters[i]);
	}
	free(harm->shifters);
	free(harm);
}

typedef struct {
	Gain* gain;
	Distortion* distortion;
	Delay* delay;
	Harmonizer* harmonizer;
}
Effects;

Effects* Effects_create(Gain* _gain, Distortion* _distortion, Delay* _delay, Harmonizer* _harmonizer) {
	Effects* fx = (Effects*)malloc(sizeof(Effects));
	fx->gain = _gain;
	fx->distortion = _distortion;
	fx->delay = _delay;
	fx->harmonizer = _harmonizer;
	return fx;
}

void Effects_destroy(Effects* fx) {
	Gain_destroy(fx->gain);
	Distortion_destroy(fx->distortion);
	Delay_destroy(fx->delay);
	Harmonizer_destroy(fx->harmonizer);
}

