#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pigpio.h>
#include <stdbool.h>
#include "portaudio.h"
#include "pa_linux_alsa.h"

#include "utility.c"
#include "sensor.c"

#define SAMPLE_RATE (44100)
#define IN_CHANNELS (1)
#define OUT_CHANNELS (1)
#define BUFFER_SIZE (128)

void sigintHandler(int sigNum) {
	printf("\nsmashed that mf ctrl-C\n");
	Pa_Terminate();
	exit(0);
}

// contains data needed by the audio processing callback
typedef struct {
	float gain;
	float distort_lvl;
}
audioData;

// simple gain scaling function
static void applyGain(float *sample, float gain) {
	*sample *= gain;
}

// wave-shaping distortion
static void distortion(float *sample, float amount) {
	float k = 2 * amount / (1 - amount);
	*sample = (1 + k) * *sample / (1 + k * abs(*sample));
}

// callback function that processes one block of audio samples at a time
static int audioCallback(const void *inputBuffer,
						 void *outputBuffer,
						 unsigned long framesPerBuffer,
						 const PaStreamCallbackTimeInfo* timeInfo,
						 PaStreamCallbackFlags statusFlags,
						 void *userData) {
	// assign typed references to audio data, input/output buffers
	audioData *data = (audioData*)userData;
	float *in = (float*)inputBuffer;
	float *out = (float*)outputBuffer;

	// loop through samples, do stuff to them
	for (unsigned int i = 0; i < framesPerBuffer; ++i) {
		*out = *in;
		applyGain(out, data->gain);
		distortion(out, data->distort_lvl);
		// increment in and out pointers
		in++;
		out++;
	}
	return 0;
}

static audioData aData;
static sensorData sData;

void audioSetup() {
	aData.gain = 0.9f;
	aData.distort_lvl = 0.0f;

	Pa_Initialize();
	
	printf("finisehd setting up audio stuff\n");
}

int main() {
	
	signal(SIGINT, sigintHandler);

	audioSetup();
	
	sensorSetup(&sData);
	PaStream *stream;
	
	PaError err;

	err = Pa_OpenDefaultStream(&stream, IN_CHANNELS, OUT_CHANNELS,
														 paFloat32, SAMPLE_RATE, BUFFER_SIZE,
														 audioCallback, &aData);
	PaAlsa_EnableRealtimeScheduling(stream, 1);
	err = Pa_StartStream(stream);
	if (err != paNoError) goto error;
	
	//~ Pa_Sleep(30 * 1000);
	float lastSmoothedDist = 0;
	while (1) {
		float distance = getCM(sData.timeoutMicros);
		if (distance != -1) {
			float smoothedDist = getAvgValue(&sData, distance);
			if (smoothedDist != lastSmoothedDist) {
				//~ printf("%f \n", smoothedDist);
				lastSmoothedDist = smoothedDist;
				aData.distort_lvl = 1 - linearScale(smoothedDist, 0, sData.maxDist, 0.0f, 0.9f);
			}
		}
		time_sleep(0.05);
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





