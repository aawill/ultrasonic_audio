#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pigpio.h>
#include "portaudio.h"
#include "pa_linux_alsa.h"
#include "math.h"
#include "time.h"

#include "utility.c"
#include "sensor.c"
#include "effects.c"

#define SAMPLE_RATE (44100)
#define ADJUSTED_SAMPLE_RATE (88200)
#define IN_CHANNELS (1)
#define OUT_CHANNELS (1)
#define CHUNK_SIZE (128)

#define GAIN_MIN (0)
#define GAIN_MAX (1)
#define DISTORT_MIN (0.2f)
#define DISTORT_MAX (0.9f)
#define DELAYSAMPS_MIN (0)
#define DELAYSAMPS_MAX (SAMPLE_RATE * 0.7f)
#define DELAYFDBK_MIN (0)
#define DELAYFDBK_MAX (0.9f)

// callback function that processes one block of audio samples at a time
static int audioCallback(const void *inputBuffer,
						 void *outputBuffer,
						 unsigned long framesPerBuffer,
						 const PaStreamCallbackTimeInfo* timeInfo,
						 PaStreamCallbackFlags statusFlags,
						 void *_fx) {
	// assign typed references to effects data, input/output buffers
	Effects* fx = (Effects*)_fx;
	float *in = (float*)inputBuffer;
	float *out = (float*)outputBuffer;
	// loop through samples, do stuff to them
	for (unsigned int i = 0; i < framesPerBuffer; ++i) {
		*out = *in;
		if (fx->gain->active) {
			*out = Gain_apply(fx->gain, *out);
		}
		if (fx->delay->active) {
			*out = Delay_apply(fx->delay, *out);
		}
		if (fx->harmonizer->active) {
			*out = Harmonizer_apply(fx->harmonizer, *out);
		}
		if (fx->distortion->active) {
			*out = Distortion_apply(fx->distortion, *out);
		}
		// increment in and out pointers
		in++;
		out++;
	}
	return 0;
}

static Effects* effects;
static Sensor* sensor1;
static Sensor* sensor2;

static void setup() {
	Gain* gain = Gain_create(GAIN_MAX);
	Distortion* dist = Distortion_create(DISTORT_MIN);
	Delay* del = Delay_create(0, 0.5f, SAMPLE_RATE, CHUNK_SIZE);
	//~ int shiftAmounts[10] = {-12, -7, 4, 7, 9, 14, 16, 19, 24};
	//~ float mixAmounts[10] = {0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8};
	//~ Harmonizer* harm = Harmonizer_create(10, shiftAmounts, mixAmounts, SAMPLE_RATE);
	//~ for (unsigned int i = 0; i < 10; ++i) {
		//~ Harmonizer_disableVoice(harm, i);
	//~ }
	int shiftAmounts[4] = {12, 7, 4, 9};
	float mixAmounts[4] = {0.9, 0.9, 0.9, 0.9};
	Harmonizer* harm = Harmonizer_create(4, shiftAmounts, mixAmounts, SAMPLE_RATE);
	for (unsigned int i = 0; i < 4; ++i) {
		Harmonizer_disableVoice(harm, i);
	}
	effects = Effects_create(gain, dist, del, harm);

	sensor1 = Sensor_create(23, 24, 10, 60, 3);
	sensor2 = Sensor_create(17, 27, 5, 50, 3);

	Pa_Initialize();

	// makes sure pigpio doesn't hog the audio peripheral we need
	gpioCfgClock(5, 0, 0);
	
	gpioInitialise();
}

static void exitHandler() {
	Pa_Terminate();
	Sensor_destroy(sensor1);
	Sensor_destroy(sensor2);
	Effects_destroy(effects);
}

int main() {
	// register teardown function to handle ctrl-c and such
	atexit(exitHandler);
	setup();
	PaError err;
	PaStream *stream;
	err = Pa_OpenDefaultStream(&stream, IN_CHANNELS, OUT_CHANNELS,
								 paFloat32, SAMPLE_RATE, CHUNK_SIZE,
								 audioCallback, effects);
	PaAlsa_EnableRealtimeScheduling(stream, 1);
	err = Pa_StartStream(stream);
	if (err != paNoError) goto error;
	float lastSmoothedDist1 = 0;
	float lastSmoothedDist2 = 0;
	int lastSubrange = -1;
	while (1) {
		float distance1 = Sensor_getCM(sensor1);
		float distance2 = Sensor_getCM(sensor2);
		if (distance1 != -1 && distance1 >= sensor1->minDist) {
			float smoothedDist1 = Sensor_getAvgValue(sensor1, distance1);
			if (smoothedDist1 != lastSmoothedDist1) {
				lastSmoothedDist1 = smoothedDist1;
				//~ printf("%f\n", smoothedDist1);
				//~ float newDistort = DISTORT_MAX - linearScale(smoothedDist1,
															 //~ sensor1->minDist, sensor1->maxDist,
															 //~ DISTORT_MIN, DISTORT_MAX);
				//~ Distortion_set(effects->distortion, newDistort);
				if (smoothedDist1 >= sensor1->maxDist * 0.75) {
					//~ printf("shutting off\n");
					effects->harmonizer->active = false;
				}
				else {
					effects->harmonizer->active = true;
					int currentSubrange = subRange(smoothedDist1, sensor1->minDist, sensor1->maxDist * 0.75, 5);
					if (currentSubrange != -1 && currentSubrange != lastSubrange) {
						lastSubrange = currentSubrange;
						//~ printf("%d\n", currentSubrange);
						if (currentSubrange == 4) {
							int voices[1] = {0};
							Harmonizer_setActiveVoices(effects->harmonizer, voices, 1);
						}
						else if (currentSubrange == 3) {
							int voices[2] = {0, 1};
							Harmonizer_setActiveVoices(effects->harmonizer, voices, 2);
						}
						else if (currentSubrange == 2) {
							int voices[3] = {0, 1, 2};
							Harmonizer_setActiveVoices(effects->harmonizer, voices, 3);
						}
						else if (currentSubrange == 1) {
							int voices[4] = {0, 1, 2, 3};
							Harmonizer_setActiveVoices(effects->harmonizer, voices, 4);
						}
					}
				}
			}
		}
		if (distance2 != -1 && distance2 >= sensor2->minDist) {
			float smoothedDist2 = Sensor_getAvgValue(sensor2, distance2);
			// lock out delay changes until current change is finished
			if (smoothedDist2 != lastSmoothedDist2 && !effects->delay->changingDelay) {
				lastSmoothedDist2 = smoothedDist2;
				// if distance is near the end of its range, shut the delay off 
				if (smoothedDist2 > sensor2->maxDist * 0.75) {
					Delay_setTime(effects->delay, 0);
					Delay_setFeedback(effects->delay, 0);
				}
				// otherwise, scale the distance to acquire new delay time and feedback values
				else {
					float newDelay = linearScale(smoothedDist2, sensor2->minDist, sensor2->maxDist,
												 DELAYSAMPS_MIN, DELAYSAMPS_MAX);
					float newFeedback = DELAYFDBK_MAX - linearScale(smoothedDist2,
														sensor2->minDist, sensor2->maxDist * 0.75,
														DELAYFDBK_MIN, DELAYFDBK_MAX);
					Delay_setTime(effects->delay, newDelay);
					Delay_setFeedback(effects->delay, newFeedback);
				}				
			}
		}
		
		time_sleep(0.01);
	}
	
	err = Pa_StopStream(stream);
	err = Pa_CloseStream(stream);
	if (err != paNoError) goto error;
	
	Pa_Terminate();
	printf("Test finished.\n");
	return err;
	
error:
    Pa_Terminate();
    fprintf( stderr, "An error occured while using the portaudio stream\n" );
    fprintf( stderr, "Error number: %d\n", err );
    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
    return err;
}





