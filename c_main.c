#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pigpio.h>
#include "portaudio.h"
#include "pa_linux_alsa.h"
#include "math.h"

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
#define DISTORT_MIN (0.01f)
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
		*out = Gain_apply(fx->gain, *out);
		*out = Distortion_apply(fx->distortion, *out);
		*out = Delay_apply(fx->delay, *out);
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
	Gain* gain = Gain_create(0.99f);
	Distortion* dist = Distortion_create(0.0f);
	Delay* del = Delay_create(0, 0.4f, SAMPLE_RATE * 4, CHUNK_SIZE);
	effects = Effects_create(gain, dist, del);

	sensor1 = Sensor_create(23, 24, 35, 3);
	sensor2 = Sensor_create(17, 27, 50, 3);

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
	while (1) {
		float distance1 = Sensor_getCM(sensor1);
		float distance2 = Sensor_getCM(sensor2);
		if (distance1 != -1) {
			float smoothedDist1 = Sensor_getAvgValue(sensor1, distance1);
			if (smoothedDist1 != lastSmoothedDist1) {
				lastSmoothedDist1 = smoothedDist1;
				float newDistort = DISTORT_MAX - linearScale(smoothedDist1,
															 0, sensor1->maxDist,
															 DISTORT_MIN, DISTORT_MAX);
				Distortion_set(effects->distortion, newDistort);
			}
		}
		if (distance2 != -1) {
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
					float newDelay = linearScale(smoothedDist2, 0, sensor2->maxDist,
												 DELAYSAMPS_MIN, DELAYSAMPS_MAX);
					float newFeedback = DELAYFDBK_MAX - linearScale(smoothedDist2,
														0, sensor2->maxDist * 0.75,
														DELAYFDBK_MIN, DELAYFDBK_MAX);
					Delay_setTime(effects->delay, newDelay);
					Delay_setFeedback(effects->delay, newFeedback);
				}				
			}
		}
		time_sleep(0.02);
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





