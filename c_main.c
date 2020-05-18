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
#define DISTORT_MAX (0.8f)
#define DELAYSAMPS_MIN (0)
#define DELAYSAMPS_MAX (SAMPLE_RATE * 0.7f)
#define DELAYFDBK_MIN (0)
#define DELAYFDBK_MAX (0.9f)
#define VOICES (4)

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
		if (fx->harmonizer->active) {
			*out = Harmonizer_apply(fx->harmonizer, *out);
		}
		if (fx->delay->active) {
			*out = Delay_apply(fx->delay, *out);
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
static Sensor* sensor3;

static void setup() {
	Gain* gain = Gain_create(GAIN_MAX);
	Distortion* dist = Distortion_create(DISTORT_MIN);
	Delay* del = Delay_create(0, 0.5f, SAMPLE_RATE, CHUNK_SIZE);
	//~ int shiftAmounts[VOICES] = {-12, -7, 4, 7, 9, 14, 16, 19, 24};
	//~ float mixAmounts[VOICES] = {0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8};
	//~ Harmonizer* harm = Harmonizer_create(VOICES, shiftAmounts, mixAmounts, SAMPLE_RATE);
	//~ for (unsigned int i = 0; i < VOICES; ++i) {
		//~ Harmonizer_disableVoice(harm, i);
	//~ }
	int shiftAmounts[VOICES] = {7, 12, 4, 9};
	float mixAmounts[VOICES] = {0.9, 0.9, 0.9, 0.9};
	Harmonizer* harm = Harmonizer_create(VOICES, shiftAmounts, mixAmounts, SAMPLE_RATE);
	for (unsigned int i = 0; i < VOICES; ++i) {
		Harmonizer_disableVoice(harm, i);
	}
	effects = Effects_create(gain, dist, del, harm);

	sensor1 = Sensor_create(5, 6, 5, 65, 3);
	sensor2 = Sensor_create(17, 27, 5, 50, 3);
	sensor3 = Sensor_create(23, 24, 5, 45, 3);

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
	float distance1 = 0;
	float distance2 = 0;
	float distance3 = 0;
	float lastDist1 = 0;
	float lastDist2 = 0;
	float lastDist3 = 0;
	int harmoZone = -1;
	int lastZone = -1;
	float zoneSize = (sensor1->maxActiveDist - sensor1->minDist) / VOICES;
	while (1) {
		distance1 = Sensor_getCM(sensor1);
		if (distance1 != -1 && distance1 >= sensor1->minDist) {
			distance1 = Sensor_getAvgValue(sensor1, distance1);
			if (distance1 != lastDist1) {
				lastDist1 = distance1;
				if (distance1 >= sensor1->maxActiveDist) {
					effects->harmonizer->active = false;
				}
				else {
					effects->harmonizer->active = true;
					for (unsigned int i = 0; i < VOICES; ++i) {
						if (distance1 <= sensor1->minDist + zoneSize * (i+1)) {
							harmoZone = VOICES - i - 1;
							if (harmoZone != lastZone) {
								// for a jump closer to sensor, add new voices
								for (int j = lastZone + 1; j <= harmoZone; ++j) {
									Harmonizer_enableVoice(effects->harmonizer, j);
								}
								// jump further, remove voices
								for (int j = lastZone; j > harmoZone; --j) {
									Harmonizer_disableVoice(effects->harmonizer, j);
								}
									
								lastZone = harmoZone;
							}
							float mixGain = 1 - linearScale(distance1,
														sensor1->minDist + zoneSize*i,
														sensor1->minDist + zoneSize*(i+1),
														0, 1);
							Harmonizer_setVoiceGain(effects->harmonizer, harmoZone, mixGain);
							break;
						}
					}
				}
			}
		}
		distance2 = Sensor_getCM(sensor2);
		if (distance2 != -1 && distance2 >= sensor2->minDist) {
			distance2 = Sensor_getAvgValue(sensor2, distance2);
			// lock out delay changes until current change is finished
			if (distance2 != lastDist2 && !effects->delay->changingDelay) {
				lastDist2 = distance2;
				// if distance is near the end of its range, shut the delay off 
				if (distance2 > sensor2->maxActiveDist) {
					Delay_setTime(effects->delay, 0);
					Delay_setFeedback(effects->delay, 0);
				}
				// otherwise, scale the distance to acquire new delay time and feedback values
				else {
					float newDelay = linearScale(distance2, sensor2->minDist, sensor2->maxDist,
												 DELAYSAMPS_MIN, DELAYSAMPS_MAX);
					float newFeedback = DELAYFDBK_MAX - linearScale(distance2,
														sensor2->minDist, sensor2->maxActiveDist,
														DELAYFDBK_MIN, DELAYFDBK_MAX);
					Delay_setTime(effects->delay, newDelay);
					Delay_setFeedback(effects->delay, newFeedback);
				}				
			}
		}
		distance3 = Sensor_getCM(sensor3);
		if (distance3 != -1 && distance3 >= sensor3->minDist) {
			distance3 = Sensor_getAvgValue(sensor3, distance3);
			if (distance3 != lastDist3) {
				lastDist3 = distance3;
				float newDistort = DISTORT_MAX - linearScale(distance3,
															 sensor1->minDist, sensor1->maxDist,
															 DISTORT_MIN, DISTORT_MAX);
				Distortion_set(effects->distortion, newDistort);
			}
		}
		time_sleep(0.06);
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





