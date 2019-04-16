/**************************************************************************
 *
 *     ooooooooo.    o8o                       
 *     `888   `Y88.  `''                       
 *      888   .d88' oooo   .ooooo.   .ooooo.   
 *      888ooo88P'  `888  d88' `'Y8 d88' `88b  
 *      888          888  888       888   888  
 *      888          888  888   .o8 888   888  
 *     o888o        o888o `Y8bod8P' `Y8bod8P'  
 *
 *
 *    Copyright Pico Technology Ltd 1995-2015
 *
 * 
 *    For sales and general information visit
 *    https://www.picotech.com   https://www.picoauto.com
 *    
 *    For help and support visit
 *    https://www.picotech.com/tech-support
 * 
 *    If you have what it takes to join us visit
 *    http://pico.jobs/
 *
 *
 * Description:
 *   This is a console mode program that demonstrates how to use the
 *   PicoScope 5000a series API.
 *
 * Examples:
 *   Collect a block of samples immediately
 *   Collect a block of samples when a trigger event occurs
 *   Collect a stream of data immediately
 *   Collect a stream of data when a trigger event occurs
 *   Set Signal Generator, using standard or custom signals
 *   Change timebase & voltage scales
 *   Display data in mV or ADC counts
 *	Handle power source changes (PicoScope5[24]xxA/B devices only)
 **************************************************************************/

#include <stdio.h>

/* Headers for Windows */
#ifdef _WIN32
#include "windows.h"
#include <conio.h>
#include "ps5000aApi.h"
#else
#include <sys/types.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include <libps5000a-1.1/ps5000aApi.h>
#ifndef PICO_STATUS
#include <libps5000a-1.1/PicoStatus.h>
#endif

#define Sleep(a) usleep(1000*a)
#define scanf_s scanf
#define fscanf_s fscanf
#define memcpy_s(a,b,c,d) memcpy(a,c,d)

typedef enum enBOOL{FALSE,TRUE} BOOL;

/* A function to detect a keyboard press on Linux */
int32_t _getch()
{
        struct termios oldt, newt;
        int32_t ch;
        int32_t bytesWaiting;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~( ICANON | ECHO );
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        setbuf(stdin, NULL);
        do {
                ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);
                if (bytesWaiting)
                        getchar();
        } while (bytesWaiting);

        ch = getchar();

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return ch;
}

int32_t _kbhit()
{
        struct termios oldt, newt;
        int32_t bytesWaiting;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~( ICANON | ECHO );
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        setbuf(stdin, NULL);
        ioctl(STDIN_FILENO, FIONREAD, &bytesWaiting);

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return bytesWaiting;
}

int32_t fopen_s(FILE ** a, const int8_t * b, const int8_t * c)
{
FILE * fp = fopen(b,c);
*a = fp;
return (fp>0)?0:-1;
}

/* A function to get a single character on Linux */
#define max(a,b) ((a) > (b) ? a : b)
#define min(a,b) ((a) < (b) ? a : b)
#endif

int32_t cycles = 0;

#define BUFFER_SIZE 	1024

#define QUAD_SCOPE		4
#define DUAL_SCOPE		2

#define MAX_PICO_DEVICES 64
#define TIMED_LOOP_STEP 500

typedef struct
{
	int16_t DCcoupled;
	int16_t range;
	int16_t enabled;
	float analogueOffset;
}CHANNEL_SETTINGS;

typedef enum
{
	MODEL_NONE = 0,
	MODEL_PS5242A = 0xA242,
	MODEL_PS5242B = 0xB242,
	MODEL_PS5243A = 0xA243,
	MODEL_PS5243B = 0xB243,
	MODEL_PS5244A = 0xA244,
	MODEL_PS5244B = 0xB244,
	MODEL_PS5442A = 0xA442,
	MODEL_PS5442B = 0xB442,
	MODEL_PS5443A = 0xA443,
	MODEL_PS5443B = 0xB443,
	MODEL_PS5444A = 0xA444,
	MODEL_PS5444B = 0xB444
} MODEL_TYPE;

typedef enum
{
	SIGGEN_NONE = 0,
	SIGGEN_FUNCTGEN = 1,
	SIGGEN_AWG = 2
} SIGGEN_TYPE;

typedef struct tTriggerDirections
{
	PS5000A_THRESHOLD_DIRECTION channelA;
	PS5000A_THRESHOLD_DIRECTION channelB;
	PS5000A_THRESHOLD_DIRECTION channelC;
	PS5000A_THRESHOLD_DIRECTION channelD;
	PS5000A_THRESHOLD_DIRECTION ext;
	PS5000A_THRESHOLD_DIRECTION aux;
}TRIGGER_DIRECTIONS;

typedef struct tPwq
{
	PS5000A_PWQ_CONDITIONS * conditions;
	int16_t nConditions;
	PS5000A_THRESHOLD_DIRECTION direction;
	uint32_t lower;
	uint32_t upper;
	PS5000A_PULSE_WIDTH_TYPE type;
}PWQ;

typedef struct
{
	int16_t handle;
	MODEL_TYPE					model;
	int8_t						modelString[8];
	int8_t						serial[10];
	int16_t						complete;
	int16_t						openStatus;
	int16_t						openProgress;
	PS5000A_RANGE				firstRange;
	PS5000A_RANGE				lastRange;
	int16_t						channelCount;
	int16_t						maxADCValue;
	SIGGEN_TYPE					sigGen;
	int16_t						hasHardwareETS;
	uint16_t					awgBufferSize;
	CHANNEL_SETTINGS			channelSettings [PS5000A_MAX_CHANNELS];
	PS5000A_DEVICE_RESOLUTION	resolution;
}UNIT;

uint32_t timebase = 8;
BOOL      scaleVoltages = TRUE;

uint16_t inputRanges [PS5000A_MAX_RANGES] = {
	10,
	20,
	50,
	100,
	200,
	500,
	1000,
	2000,
	5000,
	10000,
	20000,
	50000};

int16_t			g_autoStopped;
int16_t   		g_ready = FALSE;
uint32_t 		g_times [PS5000A_MAX_CHANNELS];
int16_t     	g_timeUnit;
int32_t      	g_sampleCount;
uint32_t		g_startIndex;
int16_t			g_trig = 0;
uint32_t		g_trigAt = 0;
int16_t			g_overflow = 0;

int8_t blockFile[20]  = "block.txt";
int8_t streamFile[20] = "stream.txt";

typedef struct tBufferInfo
{
	UNIT * unit;
	int16_t **driverBuffers;
	int16_t **appBuffers;

} BUFFER_INFO;

/****************************************************************************
* callbackStreaming
* used by PS5000A data streaming collection calls, on receipt of data.
* used to set global flags etc checked by user routines
****************************************************************************/
void PREF4 callBackStreaming(	int16_t handle,
	int32_t noOfSamples,
	uint32_t	startIndex,
	int16_t overflow,
	uint32_t triggerAt,
	int16_t triggered,
	int16_t autoStop,
	void	*pParameter)
{
	int32_t channel;
	BUFFER_INFO * bufferInfo = NULL;

	if (pParameter != NULL)
	{
		bufferInfo = (BUFFER_INFO *) pParameter;
	}

	// used for streaming
	g_sampleCount = noOfSamples;
	g_startIndex  = startIndex;
	g_autoStopped = autoStop;

	// flag to say done reading data
	g_ready = TRUE;

	// flags to show if & where a trigger has occurred
	g_trig = triggered;
	g_trigAt = triggerAt;

	g_overflow = overflow;

	if (bufferInfo != NULL && noOfSamples)
	{
		for (channel = 0; channel < bufferInfo->unit->channelCount; channel++)
		{
			if (bufferInfo->unit->channelSettings[channel].enabled)
			{
				if (bufferInfo->appBuffers && bufferInfo->driverBuffers)
				{
					// Max buffers
					if (bufferInfo->appBuffers[channel * 2]  && bufferInfo->driverBuffers[channel * 2])
					{
						memcpy_s (&bufferInfo->appBuffers[channel * 2][startIndex], noOfSamples * sizeof(int16_t),
							&bufferInfo->driverBuffers[channel * 2][startIndex], noOfSamples * sizeof(int16_t));
					}

					// Min buffers
					if (bufferInfo->appBuffers[channel * 2 + 1] && bufferInfo->driverBuffers[channel * 2 + 1])
					{
						memcpy_s (&bufferInfo->appBuffers[channel * 2 + 1][startIndex], noOfSamples * sizeof(int16_t),
							&bufferInfo->driverBuffers[channel * 2 + 1][startIndex], noOfSamples * sizeof(int16_t));
					}
				}
			}
		}
	}
}

/****************************************************************************
* Callback
* used by PS5000A data block collection calls, on receipt of data.
* used to set global flags etc checked by user routines
****************************************************************************/
void PREF4 callBackBlock( int16_t handle, PICO_STATUS status, void * pParameter)
{
	if (status != PICO_CANCELLED)
		g_ready = TRUE;
}

/****************************************************************************
* SetDefaults - restore default settings
****************************************************************************/
void setDefaults(UNIT * unit)
{
	PICO_STATUS status;
	PICO_STATUS powerStatus;
	int32_t i;

	status = ps5000aSetEts(unit->handle, PS5000A_ETS_OFF, 0, 0, NULL);					// Turn off hasHardwareETS
	printf(status?"setDefaults:ps5000aSetEts------ 0x%08lx \n":"", status);

	powerStatus = ps5000aCurrentPowerSource(unit->handle);

	for (i = 0; i < unit->channelCount; i++) // reset channels to most recent settings
	{
		if(i >= DUAL_SCOPE && powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			// No need to set the channels C and D if Quad channel scope and power not enabled.
		}
		else
		{
			status = ps5000aSetChannel(unit->handle, (PS5000A_CHANNEL)(PS5000A_CHANNEL_A + i),
				unit->channelSettings[PS5000A_CHANNEL_A + i].enabled,
				(PS5000A_COUPLING)unit->channelSettings[PS5000A_CHANNEL_A + i].DCcoupled,
				(PS5000A_RANGE)unit->channelSettings[PS5000A_CHANNEL_A + i].range, 
				unit->channelSettings[PS5000A_CHANNEL_A + i].analogueOffset);

			printf(status?"SetDefaults:ps5000aSetChannel------ 0x%08lx \n":"", status);

		}
	}
}

/****************************************************************************
* adc_to_mv
*
* Convert an 16-bit ADC count into millivolts
****************************************************************************/
int32_t adc_to_mv(int32_t raw, int32_t rangeIndex, UNIT * unit)
{
	return (raw * inputRanges[rangeIndex]) / unit->maxADCValue;
}

/****************************************************************************
* mv_to_adc
*
* Convert a millivolt value into a 16-bit ADC count
*
*  (useful for setting trigger thresholds)
****************************************************************************/
int16_t mv_to_adc(int16_t mv, int16_t rangeIndex, UNIT * unit)
{
	return (mv * unit->maxADCValue) / inputRanges[rangeIndex];
}

/****************************************************************************************
* ChangePowerSource - function to handle switches between +5V supply, and USB only power
* Only applies to PS544xA/B units 
******************************************************************************************/
PICO_STATUS changePowerSource(int16_t handle, PICO_STATUS status, UNIT * unit)
{
	int8_t ch;

	switch (status)
	{
		case PICO_POWER_SUPPLY_NOT_CONNECTED:			// User must acknowledge they want to power via USB
			do
			{
				printf("\n5V power supply not connected.");
				printf("\nDo you want to run using USB only Y/N?\n");
				ch = toupper(_getch());
				if(ch == 'Y')
				{
					printf("\nPowering the unit via USB\n");
					status = ps5000aChangePowerSource(handle, PICO_POWER_SUPPLY_NOT_CONNECTED);		// Tell the driver that's ok
				
					if(status == PICO_OK && unit->channelCount == QUAD_SCOPE)
					{
						unit->channelSettings[PS5000A_CHANNEL_C].enabled = FALSE;
						unit->channelSettings[PS5000A_CHANNEL_D].enabled = FALSE;
					}
					else if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
					{
						status = changePowerSource(handle, status, unit);
					}
					else
					{
						// Do nothing
					}

				}
			}
			while(ch != 'Y' && ch != 'N');
			printf(ch == 'N'?"Please use the +5V power supply to power this unit\n":"");
			break;

		case PICO_POWER_SUPPLY_CONNECTED:
			printf("\nUsing +5V power supply voltage\n");
			status = ps5000aChangePowerSource(handle, PICO_POWER_SUPPLY_CONNECTED);					// Tell the driver we are powered from +5V supply
			break;

		case PICO_POWER_SUPPLY_UNDERVOLTAGE:
			do
			{
				printf("\nUSB not supplying required voltage");
				printf("\nPlease plug in the +5V power supply\n");
				printf("\nHit any key to continue, or Esc to exit...\n");
				ch = _getch();
				if (ch == 0x1B)	// ESC key
					exit(0);
				else
					status = ps5000aChangePowerSource(handle, PICO_POWER_SUPPLY_CONNECTED);		// Tell the driver that's ok
			}
			while (status == PICO_POWER_SUPPLY_REQUEST_INVALID);
			break;
	}
	return status;
}

/****************************************************************************
* ClearDataBuffers
*
* stops GetData writing values to memory that has been released
****************************************************************************/
PICO_STATUS clearDataBuffers(UNIT * unit)
{
	int32_t i;
	PICO_STATUS status;

	for (i = 0; i < unit->channelCount; i++) 
	{
		if(unit->channelSettings[i].enabled)
		{
			if((status = ps5000aSetDataBuffers(unit->handle, (PS5000A_CHANNEL) i, NULL, NULL, 0, 0, PS5000A_RATIO_MODE_NONE)) != PICO_OK)
			{
				printf("clearDataBuffers:ps5000aSetDataBuffers(channel %d) ------ 0x%08lx \n", i, status);
			}
		}
	}
	return status;
}

/****************************************************************************
* BlockDataHandler
* - Used by all block data routines
* - acquires data (user sets trigger mode before calling), displays 10 items
*   and saves all to block.txt
* Input :
* - unit : the unit to use.
* - text : the text to display before the display of data slice
* - offset : the offset into the data buffer to start the display's slice.
****************************************************************************/
void blockDataHandler(UNIT * unit, int8_t * text, int32_t offset)
{
	int16_t retry;
	int16_t * buffers[PS5000A_MAX_CHANNEL_BUFFERS];

	int32_t i, j;
	int32_t timeInterval;
	int32_t sampleCount = BUFFER_SIZE;
	int32_t maxSamples;
	int32_t timeIndisposed;

	uint32_t downSampleRatio = 1;
	
	PICO_STATUS status;
	PICO_STATUS powerStatus;
	
	PS5000A_RATIO_MODE ratioMode = PS5000A_RATIO_MODE_NONE;

	FILE * fp = NULL;

	powerStatus = ps5000aCurrentPowerSource(unit->handle);
	
	for (i = 0; i < unit->channelCount; i++) 
	{
		if(i >= DUAL_SCOPE && unit->channelCount == QUAD_SCOPE && powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			// No need to set the channels C and D if Quad channel scope and power supply not connected.
		}
		else
		{
			buffers[i * 2] = (int16_t*) malloc(sampleCount * sizeof(int16_t));
			buffers[i * 2 + 1] = (int16_t*) malloc(sampleCount * sizeof(int16_t));
			
			status = ps5000aSetDataBuffers(unit->handle, (PS5000A_CHANNEL)i, buffers[i * 2], buffers[i * 2 + 1], sampleCount, 0, ratioMode);
			printf(status ? "blockDataHandler:ps5000aSetDataBuffers(channel %d) ------ 0x%08lx \n":"", i, status);
		}
	}
	

	/*  Find the maximum number of samples, the time interval (in timeUnits), the most suitable time units.
	*	If the function returns PICO_OK, the timebase will be used.
	*/
	do 
	{
		status = ps5000aGetTimebase(unit->handle, timebase, sampleCount, &timeInterval, &maxSamples, 0);

		if(status == PICO_INVALID_NUMBER_CHANNELS_FOR_RESOLUTION)
		{
			printf("BlockDataHandler: Error - Invalid number of channels for resolution.\n");
			return;
		}
		else if(status == PICO_OK)
		{
			// Do nothing
		}
		else
		{
			timebase++;
		}
	}
	while(status != PICO_OK);

	printf("\nTimebase: %lu  SampleInterval: %ldns\n", timebase, timeInterval);

	/* Start it collecting, then wait for completion*/
	g_ready = FALSE;

	do
	{
		retry = 0;

		status = ps5000aRunBlock(unit->handle, 0, sampleCount, timebase, &timeIndisposed, 0, callBackBlock, NULL);

		if(status != PICO_OK)
		{
			if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)       // 5x4xA/B devices...+5V PSU connected or removed
			{
				status = changePowerSource(unit->handle, status, unit);
				retry = 1;
			}
			else
			{
				printf("BlockDataHandler:ps5000aRunBlock ------ 0x%08lx \n", status);
				return;
			}
		}
	}
	while(retry);

	printf("Waiting for trigger...Press a key to abort\n");

	while (!g_ready && !_kbhit())
	{
		Sleep(0);
	}

	if(g_ready) 
	{
		// Can retrieve data using different ratios and ratio modes from driver
		status = ps5000aGetValues(unit->handle, 0, (uint32_t*) &sampleCount, downSampleRatio, ratioMode, 0, NULL);

		if(status != PICO_OK)
		{
			if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
			{
				if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
				{
					changePowerSource(unit->handle, status, unit);
				}
				else
				{
					printf("\nPower Source Changed. Data collection aborted.\n");
				}
			}
			else
			{
				printf("blockDataHandler:ps5000aGetValues ------ 0x%08lx \n", status);
			}
		}
		else
		{
			/* Print out the first 10 readings, converting the readings to mV if required */
			printf("%s\n",text);

			printf("Channels are in (%s):-\n\n", ( scaleVoltages ) ? ("mV") : ("ADC Counts"));

			for (j = 0; j < unit->channelCount; j++) 
			{
				if (unit->channelSettings[j].enabled) 
				{
					printf("Channel%c:    ", 'A' + j);
				}
			}
			
			printf("\n\n");

			for (i = offset; i < offset+10; i++) 
			{
				for (j = 0; j < unit->channelCount; j++) 
				{
					if (unit->channelSettings[j].enabled) 
					{
						printf("  %6d     ", scaleVoltages ? 
							adc_to_mv(buffers[j * 2][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit)	// If scaleVoltages, print mV value
							: buffers[j * 2][i]);																	// else print ADC Count
					}
				}
				
				printf("\n");
			}

			sampleCount = min(sampleCount, BUFFER_SIZE);

			fopen_s(&fp, blockFile, "w");

			if (fp != NULL)
			{
				fprintf(fp, "Block Data log\n\n");
				fprintf(fp,"Results shown for each of the %d Channels are......\n",unit->channelCount);
				fprintf(fp,"Maximum Aggregated value ADC Count & mV, Minimum Aggregated value ADC Count & mV\n\n");

				fprintf(fp, "Time  ");
				for (i = 0; i < unit->channelCount; i++) 
				{
					if (unit->channelSettings[i].enabled) 
					{
						fprintf(fp," Ch    Max ADC   Max mV  Min ADC   Min mV   ");
					}
				}
				fprintf(fp, "\n");

				for (i = 0; i < sampleCount; i++) 
				{
					fprintf(fp, "%6u ", g_times[0] + (uint32_t)(i * timeInterval));

					for (j = 0; j < unit->channelCount; j++) 
					{
						if (unit->channelSettings[j].enabled) 
						{
							fprintf(	fp,
								"Ch%C  %6d = %+6dmV, %6d = %+6dmV   ",
								'A' + j,
								buffers[j * 2][i],
								adc_to_mv(buffers[j * 2][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit),
								buffers[j * 2 + 1][i],
								adc_to_mv(buffers[j * 2 + 1][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit));
						}
					}

					fprintf(fp, "\n");
				}
				
			}
			else
			{
				printf(	"Cannot open the file %s for writing.\n"
					"Please ensure that you have permission to access the file.\n", blockFile);
			}
		} 
	}
	else 
	{
		printf("data collection aborted\n");
		_getch();
	}

	if ((status = ps5000aStop(unit->handle)) != PICO_OK)
	{
		printf("blockDataHandler:ps5000aStop ------ 0x%08lx \n", status);
	}

	if (fp != NULL)
	{
		fclose(fp);
	}
	
	for (i = 0; i < unit->channelCount; i++) 
	{
		if(unit->channelSettings[i].enabled)
		{
			free(buffers[i * 2]);
			free(buffers[i * 2 + 1]);
		}
	}
	
	clearDataBuffers(unit);
}

/****************************************************************************
* streamDataHandler
* - Used by the two stream data examples - untriggered and triggered
* Inputs:
* - unit - the unit to sample on
* - preTrigger - the number of samples in the pre-trigger phase 
*					(0 if no trigger has been set)
***************************************************************************/
void streamDataHandler(UNIT * unit, uint32_t preTrigger)
{
	int32_t i, j;
	uint32_t sampleCount = 50000; /* make sure overview buffer is large enough */
	FILE * fp = NULL;
	int16_t * buffers[PS5000A_MAX_CHANNEL_BUFFERS];
	int16_t * appBuffers[PS5000A_MAX_CHANNEL_BUFFERS];
	PICO_STATUS status;
	PICO_STATUS powerStatus;
	uint32_t sampleInterval;
	int32_t index = 0;
	int32_t totalSamples;
	uint32_t postTrigger;
	int16_t autostop;
	uint32_t downsampleRatio;
	uint32_t triggeredAt = 0;
	PS5000A_TIME_UNITS timeUnits;
	PS5000A_RATIO_MODE ratioMode;
	int16_t retry = 0;
	int16_t powerChange = 0;
	uint32_t numStreamingValues = 0;

	BUFFER_INFO bufferInfo;

	powerStatus = ps5000aCurrentPowerSource(unit->handle);
	
	for (i = 0; i < unit->channelCount; i++) 
	{
		if(i >= DUAL_SCOPE && unit->channelCount == QUAD_SCOPE && powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			// No need to set the channels C and D if Quad channel scope and power supply not connected.
		}
		else
		{

			buffers[i * 2] = (int16_t*) calloc(sampleCount, sizeof(int16_t));
			buffers[i * 2 + 1] = (int16_t*) calloc(sampleCount, sizeof(int16_t));
			
			status = ps5000aSetDataBuffers(unit->handle, (PS5000A_CHANNEL)i, buffers[i * 2], buffers[i * 2 + 1], sampleCount, 0, PS5000A_RATIO_MODE_NONE);

			appBuffers[i * 2] = (int16_t*) calloc(sampleCount, sizeof(int16_t));
			appBuffers[i * 2 + 1] = (int16_t*) calloc(sampleCount, sizeof(int16_t));

			printf(status?"StreamDataHandler:ps5000aSetDataBuffers(channel %ld) ------ 0x%08lx \n":"", i, status);
		}
	}
	
	downsampleRatio = 1;
	timeUnits = PS5000A_US;
	sampleInterval = 1;
	ratioMode = PS5000A_RATIO_MODE_NONE;
	preTrigger = 0;
	postTrigger = 1000000;
	autostop = TRUE;
	
	bufferInfo.unit = unit;	
	bufferInfo.driverBuffers = buffers;
	bufferInfo.appBuffers = appBuffers;

	if (autostop)
	{
		printf("\nStreaming Data for %lu samples", postTrigger / downsampleRatio);
		
		if (preTrigger)							// we pass 0 for preTrigger if we're not setting up a trigger
		{
			printf(" after the trigger occurs\nNote: %lu Pre Trigger samples before Trigger arms\n\n",preTrigger / downsampleRatio);
		}
		else
		{
			printf("\n\n");
		}
	}
	else
	{
		printf("\nStreaming Data continually.\n\n");
	}

	g_autoStopped = FALSE;


	do
	{
		retry = 0;

		status = ps5000aRunStreaming(unit->handle, &sampleInterval, timeUnits, preTrigger, postTrigger, autostop, downsampleRatio, ratioMode,
			sampleCount);

		if(status != PICO_OK)
		{
			if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
			{
				status = changePowerSource(unit->handle, status, unit);
				retry = 1;
			}
			else
			{
				printf("streamDataHandler:ps5000aRunStreaming ------ 0x%08lx \n", status);
				return;
			}
		}
	}
	while(retry);

	printf("Streaming data...Press a key to stop\n");

	
	fopen_s(&fp, streamFile, "w");

	if (fp != NULL)
	{
		fprintf(fp,"For each of the %d Channels, results shown are....\n",unit->channelCount);
		fprintf(fp,"Maximum Aggregated value ADC Count & mV, Minimum Aggregated value ADC Count & mV\n\n");

		for (i = 0; i < unit->channelCount; i++) 
		{
			if (unit->channelSettings[i].enabled) 
			{
				fprintf(fp,"   Max ADC    Max mV  Min ADC  Min mV   ");
			}
		}
		fprintf(fp, "\n");
	}
	

	totalSamples = 0;

	while (!_kbhit() && !g_autoStopped)
	{
		/* Poll until data is received. Until then, GetStreamingLatestValues wont call the callback */
		g_ready = FALSE;

		status = ps5000aGetStreamingLatestValues(unit->handle, callBackStreaming, &bufferInfo);

		if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || status == PICO_POWER_SUPPLY_UNDERVOLTAGE) // 5x4xA/B devices...+5V PSU connected or removed
		{
			if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
			{
				changePowerSource(unit->handle, status, unit);
			}

			printf("\n\nPower Source Change");
			powerChange = 1;
		}

		index ++;

		if (g_ready && g_sampleCount > 0) /* can be ready and have no data, if autoStop has fired */
		{
			if (g_trig)
			{
				triggeredAt = totalSamples + g_trigAt;		// calculate where the trigger occurred in the total samples collected
			}

			totalSamples += g_sampleCount;
			printf("\nCollected %3li samples, index = %5lu, Total: %6d samples ", g_sampleCount, g_startIndex, totalSamples);
			
			if (g_trig)
			{
				printf("Trig. at index %lu total %lu", g_trigAt, triggeredAt + 1);	// show where trigger occurred
				
			}
			
			for (i = g_startIndex; i < (int32_t)(g_startIndex + g_sampleCount); i++) 
			{
				
				if(fp != NULL)
				{
					for (j = 0; j < unit->channelCount; j++) 
					{
						if (unit->channelSettings[j].enabled) 
						{
							fprintf(	fp,
								"Ch%C  %5d = %+5dmV, %5d = %+5dmV   ",
								(char)('A' + j),
								appBuffers[j * 2][i],
								adc_to_mv(appBuffers[j * 2][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit),
								appBuffers[j * 2 + 1][i],
								adc_to_mv(appBuffers[j * 2 + 1][i], unit->channelSettings[PS5000A_CHANNEL_A + j].range, unit));
						}
					}

					fprintf(fp, "\n");
				}
				else
				{
					printf("Cannot open the file %s for writing.\n", streamFile);
				}
				
			}
		}
	}

	printf("\n\n");

	ps5000aStop(unit->handle);

	if (fp != NULL)
	{

		fclose (fp);
	}

	if (!g_autoStopped && !powerChange)  
	{
		printf("\nData collection aborted\n");
		_getch();
	}
	else
	{
		printf("\nData collection complete.\n\n");
	}
	
	for (i = 0; i < unit->channelCount; i++) 
	{
		if(unit->channelSettings[i].enabled)
		{
			free(buffers[i * 2]);
			free(appBuffers[i * 2]);

			free(buffers[i * 2 + 1]);
			free(appBuffers[i * 2 + 1]);
		}
	}

	clearDataBuffers(unit);
}


/****************************************************************************
* setTrigger
*
* - Used to call all the functions required to set up triggering
*
***************************************************************************/
PICO_STATUS setTrigger(	UNIT * unit,
	struct tPS5000ATriggerChannelProperties * channelProperties,
	int16_t nChannelProperties,
	PS5000A_TRIGGER_CONDITIONS * triggerConditions,
	int16_t nTriggerConditions,
	TRIGGER_DIRECTIONS * directions,
	struct tPwq * pwq,
	uint32_t delay,
	int16_t auxOutputEnabled,
	int32_t autoTriggerMs)
{
	PICO_STATUS status;

	if ((status = ps5000aSetTriggerChannelProperties(unit->handle,
		channelProperties,
		nChannelProperties,
		auxOutputEnabled,
		autoTriggerMs)) != PICO_OK) 
	{
		printf("setTrigger:ps5000aSetTriggerChannelProperties ------ Ox%08lx \n", status);
		return status;
	}

	if ((status = ps5000aSetTriggerChannelConditions(unit->handle, triggerConditions, nTriggerConditions)) != PICO_OK)
	{
		printf("setTrigger:ps5000aSetTriggerChannelConditions ------ 0x%08lx \n", status);
		return status;
	}

	if ((status = ps5000aSetTriggerChannelDirections(unit->handle,
		directions->channelA,
		directions->channelB,
		directions->channelC,
		directions->channelD,
		directions->ext,
		directions->aux)) != PICO_OK) 
	{
		printf("setTrigger:ps5000aSetTriggerChannelDirections ------ 0x%08lx \n", status);
		return status;
	}

	if ((status = ps5000aSetTriggerDelay(unit->handle, delay)) != PICO_OK)
	{
		printf("setTrigger:ps5000aSetTriggerDelay ------ 0x%08lx \n", status);
		return status;
	}

	if((status = ps5000aSetPulseWidthQualifier(unit->handle,
		pwq->conditions,
		pwq->nConditions, 
		pwq->direction,
		pwq->lower, 
		pwq->upper, 
		pwq->type)) != PICO_OK)
	{
		printf("setTrigger:ps5000aSetPulseWidthQualifier ------ 0x%08lx \n", status);
		return status;
	}

	return status;
}

/****************************************************************************
* collectBlockImmediate
*  this function demonstrates how to collect a single block of data
*  from the unit (start collecting immediately)
****************************************************************************/
void collectBlockImmediate(UNIT * unit)
{
	struct tPwq pulseWidth;
	struct tTriggerDirections directions;

	memset(&directions, 0, sizeof(struct tTriggerDirections));
	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect block immediate...\n");
	printf("Press a key to start\n");
	_getch();

	setDefaults(unit);

	/* Trigger disabled	*/
	setTrigger(unit, NULL, 0, NULL, 0, &directions, &pulseWidth, 0, 0, 0);

	blockDataHandler(unit, (int8_t *) "First 10 readings\n", 0);
}

/****************************************************************************
* collectBlockEts
*  this function demonstrates how to collect a block of
*  data using equivalent time sampling (ETS).
****************************************************************************/
void collectBlockEts(UNIT * unit)
{
	PICO_STATUS status;
	int32_t ets_sampletime;
	int16_t	triggerVoltage = mv_to_adc(1000,	unit->channelSettings[PS5000A_CHANNEL_A].range, unit);
	uint32_t delay = 0;
	struct tPwq pulseWidth;
	struct tTriggerDirections directions;

	struct tPS5000ATriggerChannelProperties sourceDetails = {	triggerVoltage,
		256 * 10,
		triggerVoltage,
		256 * 10,
		PS5000A_CHANNEL_A,
		PS5000A_LEVEL };

	struct tPS5000ATriggerConditions conditions = {	PS5000A_CONDITION_TRUE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE};

	memset(&pulseWidth, 0, sizeof(struct tPwq));
	memset(&directions, 0, sizeof(struct tTriggerDirections));
	directions.channelA = PS5000A_RISING;

	printf("Collect ETS block...\n");
	printf("Collects when value rises past %d", scaleVoltages? 
		adc_to_mv(sourceDetails.thresholdUpper,	unit->channelSettings[PS5000A_CHANNEL_A].range, unit)	// If scaleVoltages, print mV value
		: sourceDetails.thresholdUpper);																// else print ADC Count
	
	printf(scaleVoltages? "mV\n" : "ADC Counts\n");
	printf("Press a key to start...\n");
	_getch();

	setDefaults(unit);

	//Trigger enabled
	//Rising edge
	//Threshold = 1000mV
	status = setTrigger(unit, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, delay, 0, 0);

	status = ps5000aSetEts(unit->handle, PS5000A_ETS_FAST, 20, 4, &ets_sampletime);
	printf("ETS Sample Time is: %ld\n", ets_sampletime);

	blockDataHandler(unit, (int8_t *) "Ten readings after trigger\n", BUFFER_SIZE / 10 - 5); // 10% of data is pre-trigger

	status = ps5000aSetEts(unit->handle, PS5000A_ETS_OFF, 0, 0, &ets_sampletime);
}

/****************************************************************************
* collectBlockTriggered
*  this function demonstrates how to collect a single block of data from the
*  unit, when a trigger event occurs.
****************************************************************************/
void collectBlockTriggered(UNIT * unit)
{
	int16_t triggerVoltage = mv_to_adc(1000, unit->channelSettings[PS5000A_CHANNEL_A].range, unit);

	struct tPS5000ATriggerChannelProperties sourceDetails = {	triggerVoltage,
		256 * 10,
		triggerVoltage,
		256 * 10,
		PS5000A_CHANNEL_A,
		PS5000A_LEVEL};

	struct tPS5000ATriggerConditions conditions = {	PS5000A_CONDITION_TRUE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE};

	struct tPwq pulseWidth;

	struct tTriggerDirections directions = { PS5000A_RISING,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE };

	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect block triggered...\n");
	printf("Collects when value rises past %d", scaleVoltages?
		adc_to_mv(sourceDetails.thresholdUpper, unit->channelSettings[PS5000A_CHANNEL_A].range, unit)	// If scaleVoltages, print mV value
		: sourceDetails.thresholdUpper);																// else print ADC Count
	
	printf(scaleVoltages?"mV\n" : "ADC Counts\n");

	printf("Press a key to start...\n");
	_getch();

	setDefaults(unit);

	/* Trigger enabled
	* Rising edge
	* Threshold = 1000mV */
	setTrigger(unit, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, 0, 0, 0);

	blockDataHandler(unit, (int8_t *) "Ten readings after trigger\n", 0);
}

/****************************************************************************
* collectRapidBlock
*  this function demonstrates how to collect a set of captures using 
*  rapid block mode.
****************************************************************************/
void collectRapidBlock(UNIT * unit)
{
	uint32_t nCaptures;
	uint32_t nSegments;
	int32_t nMaxSamples;
	uint32_t nSamples = 1000;
	int32_t timeIndisposed;
	uint32_t capture; 
	int16_t channel;
	int16_t ***rapidBuffers;
	int16_t *overflow;
	PICO_STATUS status;
	int16_t i;
	uint32_t nCompletedCaptures;
	int16_t retry;

	int16_t	triggerVoltage = mv_to_adc(1000, unit->channelSettings[PS5000A_CHANNEL_A].range, unit);

	struct tPS5000ATriggerChannelProperties sourceDetails = { triggerVoltage,
		256 * 10,
		triggerVoltage,
		256 * 10,
		PS5000A_CHANNEL_A,
		PS5000A_LEVEL};

	struct tPS5000ATriggerConditions conditions = {	PS5000A_CONDITION_TRUE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE};

	struct tPwq pulseWidth;

	struct tTriggerDirections directions = { PS5000A_RISING,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE };

	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect rapid block triggered...\n");
	printf("Collects when value rises past %d",	scaleVoltages?
		adc_to_mv(sourceDetails.thresholdUpper, unit->channelSettings[PS5000A_CHANNEL_A].range, unit)	// If scaleVoltages, print mV value
		: sourceDetails.thresholdUpper);																// else print ADC Count
	
	printf(scaleVoltages?"mV\n" : "ADC Counts\n");
	printf("Press any key to abort\n");

	setDefaults(unit);

	// Trigger enabled
	setTrigger(unit, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, 0, 0, 0);

	// Set the number of segments
	nSegments = 64;

	//Set the number of captures
	nCaptures = 10;

	//Segment the memory
	status = ps5000aMemorySegments(unit->handle, nSegments, &nMaxSamples);

	//Set the number of captures
	status = ps5000aSetNoOfCaptures(unit->handle, nCaptures);

	//Run
	timebase = 127;		// 1 MS/s at 8-bit resolution, ~504kS/s at 12 & 16-bit resolution

	do
	{
		retry = 0;
		status = ps5000aRunBlock(unit->handle, 0, nSamples, timebase, &timeIndisposed, 0, callBackBlock, NULL);


		if(status != PICO_OK)
		{
			if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED)
			{
				status = changePowerSource(unit->handle, status, unit);
				retry = 1;
			}
			else
			{
				printf("collectRapidBlock:ps5000aRunBlock ------ 0x%08lx \n", status);
			}
		}
	}
	while(retry);

	//Wait until data ready
	g_ready = 0;

	while(!g_ready && !_kbhit())
	{
		Sleep(0);
	}

	if(!g_ready)
	{
		_getch();
		status = ps5000aStop(unit->handle);
		status = ps5000aGetNoOfCaptures(unit->handle, &nCompletedCaptures);
		
		printf("Rapid capture aborted. %lu complete blocks were captured\n", nCompletedCaptures);
		printf("\nPress any key...\n\n");
		_getch();

		if(nCompletedCaptures == 0)
		{
			return;
		}

		//Only display the blocks that were captured
		nCaptures = (uint16_t)nCompletedCaptures;
	}

	//Allocate memory
	rapidBuffers = (int16_t ***) calloc(unit->channelCount, sizeof(int16_t*));
	overflow = (int16_t *) calloc(unit->channelCount * nCaptures, sizeof(int16_t));

	for (channel = 0; channel < unit->channelCount; channel++) 
	{
		rapidBuffers[channel] = (int16_t **) calloc(nCaptures, sizeof(int16_t*));
	}

	for (channel = 0; channel < unit->channelCount; channel++) 
	{	
		if(unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++) 
			{
				rapidBuffers[channel][capture] = (int16_t *) calloc(nSamples, sizeof(int16_t));
			}
		}
	}

	for (channel = 0; channel < unit->channelCount; channel++) 
	{
		if(unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++) 
			{
				status = ps5000aSetDataBuffer(unit->handle, (PS5000A_CHANNEL)channel, rapidBuffers[channel][capture], nSamples, capture, PS5000A_RATIO_MODE_NONE);
			}
		}
	}

	//Get data
	status = ps5000aGetValuesBulk(unit->handle, &nSamples, 0, nCaptures - 1, 1, PS5000A_RATIO_MODE_NONE, overflow);

	if (status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED)
	{
		printf("\nPower Source Changed. Data collection aborted.\n");
	}

	if (status == PICO_OK)
	{
		//print first 10 samples from each capture
		for (capture = 0; capture < nCaptures; capture++)
		{
			printf("\nCapture %d:-\n\n", capture + 1);
			
			for (channel = 0; channel < unit->channelCount; channel++) 
			{
				if(unit->channelSettings[channel].enabled)
				{
					printf("Channel %c:\t", 'A' + channel);
				}
			}
			printf("\n");

			for(i = 0; i < 10; i++)
			{
				for (channel = 0; channel < unit->channelCount; channel++) 
				{
					if(unit->channelSettings[channel].enabled)
					{
						printf("   %6d       ", scaleVoltages ? 
							adc_to_mv(rapidBuffers[channel][capture][i], unit->channelSettings[PS5000A_CHANNEL_A +channel].range, unit)	// If scaleVoltages, print mV value
							: rapidBuffers[channel][capture][i]);																	// else print ADC Count
					}
				}

				printf("\n");
			}
		}
	}

	//Stop
	status = ps5000aStop(unit->handle);

	//Free memory
	free(overflow);

	for (channel = 0; channel < unit->channelCount; channel++) 
	{	
		if(unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++) 
			{
				free(rapidBuffers[channel][capture]);
			}
		}
	}

	for (channel = 0; channel < unit->channelCount; channel++) 
	{
		free(rapidBuffers[channel]);
	}

	free(rapidBuffers);
}

/****************************************************************************
* Initialise unit' structure with Variant specific defaults
****************************************************************************/
void set_info(UNIT * unit)
{
	int8_t description [11][25]= { "Driver Version",
		"USB Version",
		"Hardware Version",
		"Variant Info",
		"Serial",
		"Cal Date",
		"Kernel Version",
		"Digital HW Version",
		"Analogue HW Version",
		"Firmware 1",
		"Firmware 2"};

	int16_t i = 0;
	int16_t requiredSize = 0;
	int8_t line [10];
	int32_t variant;
	PICO_STATUS status = PICO_OK;

	if (unit->handle) 
	{
		for (i = 0; i < 11; i++) 
		{
			status = ps5000aGetUnitInfo(unit->handle, line, sizeof (line), &requiredSize, i);

			// info = 3 - PICO_VARIANT_INFO
			if(i == PICO_VARIANT_INFO) 
			{
				variant = atoi(line);
				memcpy(&(unit->modelString), line, sizeof(unit->modelString)==5?5:sizeof(unit->modelString));
				
				//To identify variants.....
				if (strlen(line) == 5)						// A or B variant unit
				{
					line[4] = toupper(line[4]);

					if (line[1] == '2' && line[4] == 'A')		// i.e 5244A -> 0xA244
					{
						variant += 0x8DC8;
					}
					else
					{
						if (line[1] == '2' && line[4] == 'B')		//i.e 5244B -> 0xB244
						{
							variant +=0x9DC8;
						}
						else
						{
							if (line[1] == '4' && line[4] == 'A')		// i.e 5444A -> 0xA444
							{
								variant += 0x8F00;
							}
							else
							{
								if (line[1] == '4' && line[4] == 'B')		//i.e 5444B -> 0xB444
								{
									variant +=0x9F00;
								}
							}
						}
					}
				}
			}
			else if(i == PICO_BATCH_AND_SERIAL)	// info = 4 - PICO_BATCH_AND_SERIAL
			{
				memcpy(&(unit->serial), line, requiredSize);
			}

			printf("%s: %s\n", description[i], line);
		}

		printf("\n");

		switch (variant)
		{
			case MODEL_PS5242A:
				unit->model			= MODEL_PS5242A;
				unit->sigGen		= SIGGEN_FUNCTGEN;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->hasHardwareETS			= FALSE;
				unit->awgBufferSize	= 0;
				break;

			case MODEL_PS5242B:
				unit->model			= MODEL_PS5242B;
				unit->sigGen		= SIGGEN_AWG;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->hasHardwareETS			= FALSE;
				unit->awgBufferSize	= PS5X42A_MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS5243A:
				unit->model			= MODEL_PS5243A;
				unit->sigGen		= SIGGEN_FUNCTGEN;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->hasHardwareETS			= TRUE;
				unit->awgBufferSize	= 0;
				break;

			case MODEL_PS5243B:
				unit->model			= MODEL_PS5243B;
				unit->sigGen		= SIGGEN_AWG;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->hasHardwareETS			= TRUE;
				unit->awgBufferSize	= PS5X43A_MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS5244A:
				unit->model			= MODEL_PS5244A;
				unit->sigGen		= SIGGEN_FUNCTGEN;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->hasHardwareETS			= TRUE;
				unit->awgBufferSize	= 0;
				break;

			case MODEL_PS5244B:
				unit->model			= MODEL_PS5244B;
				unit->sigGen		= SIGGEN_AWG;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->hasHardwareETS			= TRUE;
				unit->awgBufferSize	= PS5X44A_MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS5442A:
				unit->model			= MODEL_PS5442A;
				unit->sigGen		= SIGGEN_FUNCTGEN;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->hasHardwareETS			= FALSE;
				unit->awgBufferSize	= 0;
				break;

			case MODEL_PS5442B:
				unit->model			= MODEL_PS5442B;
				unit->sigGen		= SIGGEN_AWG;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->hasHardwareETS			= FALSE;
				unit->awgBufferSize	= PS5X42A_MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS5443A:
				unit->model			= MODEL_PS5443A;
				unit->sigGen		= SIGGEN_FUNCTGEN;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->hasHardwareETS			= TRUE;
				unit->awgBufferSize	= 0;
				break;

			case MODEL_PS5443B:
				unit->model			= MODEL_PS5443B;
				unit->sigGen		= SIGGEN_AWG;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->hasHardwareETS			= TRUE;
				unit->awgBufferSize	= PS5X43A_MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS5444A:
				unit->model			= MODEL_PS5444A;
				unit->sigGen		= SIGGEN_FUNCTGEN;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->hasHardwareETS			= TRUE;
				unit->awgBufferSize	= 0;
				break;

			case MODEL_PS5444B:
				unit->model			= MODEL_PS5444B;
				unit->sigGen		= SIGGEN_AWG;
				unit->firstRange	= PS5000A_10MV;
				unit->lastRange		= PS5000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->hasHardwareETS			= TRUE;
				unit->awgBufferSize	= PS5X44A_MAX_SIG_GEN_BUFFER_SIZE;
				break;

			default:
				unit->model			= MODEL_NONE;
				break;
		}
	}
}

/****************************************************************************
* Select input voltage ranges for channels
****************************************************************************/
void setVoltages(UNIT * unit)
{
	PICO_STATUS powerStatus = PICO_OK;
	PICO_STATUS status = PICO_OK;
	PS5000A_DEVICE_RESOLUTION resolution = PS5000A_DR_8BIT;

	int32_t i, ch;
	int32_t count = 0;
	int16_t numValidChannels = unit->channelCount; // Dependent on power setting - i.e. channel A & B if USB powered on 4-channel device
	int16_t numEnabledChannels = 0;
	int16_t retry = FALSE;

	if(unit->channelCount == QUAD_SCOPE)
	{
		powerStatus = ps5000aCurrentPowerSource(unit->handle); 

		if(powerStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			numValidChannels = DUAL_SCOPE;
		}
	}

	// See what ranges are available... 
	for (i = unit->firstRange; i <= unit->lastRange; i++) 
	{
		printf("%d -> %d mV\n", i, inputRanges[i]);
	}

	do
	{
		count = 0;

		do
		{
			// Ask the user to select a range
			printf("Specify voltage range (%d..%d)\n", unit->firstRange, unit->lastRange);
			printf("99 - switches channel off\n");
		
			for (ch = 0; ch < numValidChannels; ch++) 
			{
				printf("\n");

				do 
				{
					printf("Channel %c: ", 'A' + ch);
					fflush(stdin);
					scanf_s("%hd", &(unit->channelSettings[ch].range));
			
				} while (unit->channelSettings[ch].range != 99 && (unit->channelSettings[ch].range < unit->firstRange || unit->channelSettings[ch].range > unit->lastRange));

				if (unit->channelSettings[ch].range != 99) 
				{
					printf(" - %d mV\n", inputRanges[unit->channelSettings[ch].range]);
					unit->channelSettings[ch].enabled = TRUE;
					count++;
				} 
				else 
				{
					printf("Channel Switched off\n");
					unit->channelSettings[ch].enabled = FALSE;
					unit->channelSettings[ch].range = PS5000A_MAX_RANGES-1;
				}
			}
			printf(count == 0? "\n** At least 1 channel must be enabled **\n\n":"");
		}
		while(count == 0);	// must have at least one channel enabled

		status = ps5000aGetDeviceResolution(unit->handle, &resolution);

		// Verify that the number of enabled channels is valid for the resolution set.

		switch(resolution)
		{
			case PS5000A_DR_15BIT:

				if(count > 2)
				{
					printf("\nError: Only 2 channels may be enabled with 15-bit resolution set.\n");
					printf("Please switch off %d channel(s).\n", numValidChannels - 2);
					retry = TRUE;
				}
				else
				{
					retry = FALSE;
				}
				break;

			case PS5000A_DR_16BIT:

				if(count > 1)
				{
					printf("\nError: Only one channes may be enabled with 16-bit resolution set.\n");
					printf("Please switch off %d channel(s).\n", numValidChannels - 1);
					retry = TRUE;
				}
				else
				{
					retry = FALSE;
				}
				
				break;

			default:

				retry = FALSE;
				break;
		}

		printf("\n");
	}
	while(retry == TRUE);

	setDefaults(unit);	// Put these changes into effect
}

/****************************************************************************
* setTimebase
* Select timebase, set time units as nano seconds
*
****************************************************************************/
void setTimebase(UNIT * unit)
{
	PICO_STATUS status = PICO_OK;
	int32_t timeInterval;
	int32_t maxSamples;

	printf("Specify desired timebase: ");
	fflush(stdin);
	scanf_s("%lud", &timebase);

	do 
	{
		status = ps5000aGetTimebase(unit->handle, timebase, BUFFER_SIZE, &timeInterval, &maxSamples, 0);

		if(status == PICO_INVALID_NUMBER_CHANNELS_FOR_RESOLUTION)
		{
			printf("SetTimebase: Error - Invalid number of channels for resolution.\n");
			return;
		}
		else if(status == PICO_OK)
		{
			// Do nothing
		}
		else
		{
			timebase++; // Increase timebase if the one specified can't be used. 
		}

	}
	while(status != PICO_OK);

	printf("Timebase used %lu = %ldns sample interval\n", timebase, timeInterval);
}

/****************************************************************************
* printResolution
*
* Outputs the resolution in text format to the console window
****************************************************************************/
void printResolution(PS5000A_DEVICE_RESOLUTION * resolution)
{
	switch(*resolution)
	{
		case PS5000A_DR_8BIT:

			printf("8 bits");
			break;

		case PS5000A_DR_12BIT:

			printf("12 bits");
			break;

		case PS5000A_DR_14BIT:

			printf("14 bits");
			break;

		case PS5000A_DR_15BIT:

			printf("15 bits");
			break;

		case PS5000A_DR_16BIT:

			printf("16 bits");
			break;

		default:

			break;
	}

	printf("\n");
}

/****************************************************************************
* setResolution
* Set resolution for the device
*
****************************************************************************/
void setResolution(UNIT * unit)
{
	int16_t value;
	int16_t i;
	int16_t numEnabledChannels = 0;
	int16_t retry;

	PICO_STATUS status;
	PS5000A_DEVICE_RESOLUTION resolution;
	PS5000A_DEVICE_RESOLUTION newResolution = PS5000A_DR_8BIT;

	// Determine number of channels enabled
	for(i = 0; i < unit->channelCount; i++)
	{
		if(unit->channelSettings[i].enabled == TRUE)
		{
			numEnabledChannels++;
		}
	}

	if(numEnabledChannels == 0)
	{
		printf("setResolution: Please enable channels.\n");
		return;
	}

	status = ps5000aGetDeviceResolution(unit->handle, &resolution);

	if(status == PICO_OK)
	{
		printf("Current resolution: ");
		printResolution(&resolution);
	}
	else
	{
		printf("setResolution:ps5000aGetDeviceResolution ------ 0x%08lx \n", status);
		return;
	}

	printf("\n");

	printf("Select device resolution:\n");
	printf("0: 8 bits\n");
	printf("1: 12 bits\n");
	printf("2: 14 bits\n");

	if(numEnabledChannels <= 2)
	{
		printf("3: 15 bits\n");
	}

	if(numEnabledChannels == 1)
	{
		printf("4: 16 bits\n\n");
	}

	retry = TRUE;

	do
	{
		if(numEnabledChannels == 1)
		{
			printf("Resolution [0...4]: ");
		}
		else if(numEnabledChannels == 2)
		{
			printf("Resolution [0...3]: ");
		}
		else
		{
			printf("Resolution [0...2]: ");
		}
	
		fflush(stdin);
		scanf_s("%lud", &newResolution);

		// Verify if resolution can be selected for number of channels enabled

		if(newResolution == PS5000A_DR_16BIT && numEnabledChannels > 1)
		{
			printf("setResolution: 16 bit resolution can only be selected with 1 channel enabled.\n");
		}
		else if(newResolution == PS5000A_DR_15BIT && numEnabledChannels > 2)
		{
			printf("setResolution: 15 bit resolution can only be selected with a maximum of 2 channels enabled.\n");
		}
		else if(newResolution < PS5000A_DR_8BIT && newResolution > PS5000A_DR_16BIT)
		{
			printf("setResolution: Resolution index selected out of bounds.\n");
		}
		else
		{
			retry = FALSE;
		}
	}
	while(retry);
	
	printf("\n");

	status = ps5000aSetDeviceResolution(unit->handle, (PS5000A_DEVICE_RESOLUTION) newResolution);

	if(status == PICO_OK)
	{
		unit->resolution = newResolution;

		printf("Resolution selected: ");
		printResolution(&newResolution);
		
		// The maximum ADC value will change if transitioning from 8 bit to >= 12 bit or vice-versa
		ps5000aMaximumValue(unit->handle, &value);
		unit->maxADCValue = value;
		
		// Update first range depending on resolution
		if(newResolution == PS5000A_DR_8BIT)
		{
			unit->firstRange = PS5000A_10MV;
		}
		else if(newResolution == PS5000A_DR_12BIT)
		{
			unit->firstRange = PS5000A_20MV;
		}
		else
		{
			unit->firstRange = PS5000A_50MV;
		}

	}
	else
	{
		printf("setResolution:ps5000aSetDeviceResolution ------ 0x%08lx \n", status);
	}

}

/****************************************************************************************************
* setSignalGenerator
* Sets the signal generator
* - allows user to set frequency and waveform
* - allows for custom waveform (values -32768..32767) 
* - of up to 16384 samples (PS5X42B), 32768 samples (PS5X43B), or 49152 samples (PS5X44B)
****************************************************************************************************/
void setSignalGenerator(UNIT * unit)
{
	PICO_STATUS status;
	int16_t waveform;
	double frequency = 1.0;
	int8_t fileName [128];
	FILE * fp = NULL;
	int16_t * arbitraryWaveform;
	int32_t waveformSize = 0;
	uint32_t pkpk = 4000000;	//2V
	int32_t offset = 0;
	int8_t ch;
	int16_t choice;
	uint32_t deltaPhase = 0;

	while (_kbhit())			// use up keypress
		_getch();

	do
	{
		printf("\nSignal Generator\n================\n");
		printf("0 - SINE         1 - SQUARE\n");
		printf("2 - TRIANGLE     3 - DC VOLTAGE\n");
		if(unit->sigGen == SIGGEN_AWG)
		{
			printf("4 - RAMP UP      5 - RAMP DOWN\n");
			printf("6 - SINC         7 - GAUSSIAN\n");
			printf("8 - HALF SINE    A - AWG WAVEFORM\n");
		}
		printf("F - SigGen Off\n\n");

		ch = _getch();

		if (ch >= '0' && ch <='9')
		{
			choice = ch -'0';
		}
		else
		{
			ch = toupper(ch);
		}
	}
	while((unit->sigGen == SIGGEN_FUNCTGEN && ch != 'F' && 
		(ch < '0' || ch > '3')) || (unit->sigGen == SIGGEN_AWG && ch != 'A' && ch != 'F' && (ch < '0' || ch > '8')));

	if(ch == 'F')			// If we're going to turn off siggen
	{
		printf("Signal generator Off\n");
		waveform = PS5000A_DC_VOLTAGE;		// DC Voltage
		pkpk = 0;							// 0V
		waveformSize = 0;
	}
	else
	{
		if (ch == 'A' && unit->sigGen == SIGGEN_AWG)		// Set the AWG
		{
			arbitraryWaveform = (int16_t*)malloc( unit->awgBufferSize * sizeof(int16_t));
			memset(arbitraryWaveform, 0, unit->awgBufferSize * sizeof(int16_t));

			waveformSize = 0;

			printf("Select a waveform file to load: ");
			scanf_s("%s", fileName, 128);

			if (fopen_s(&fp, fileName, "r") == 0) 
			{ // Having opened file, read in data - one number per line (max 16384 lines for PS5X42B device, 
			  // 32768 for PS5X43B, 49152 for PS5X44B), with values in (-32768..+32767)
				while (EOF != fscanf_s(fp, "%hi", (arbitraryWaveform + waveformSize)) && waveformSize++ < unit->awgBufferSize - 1);
				fclose(fp);
				printf("File successfully loaded\n");
			} 
			else 
			{
				printf("Invalid filename\n");
				return;
			}
		}
		else			// Set one of the built in waveforms
		{
			switch (choice)
			{
				case 0:
					waveform = PS5000A_SINE;
					break;

				case 1:
					waveform = PS5000A_SQUARE;
					break;

				case 2:
					waveform = PS5000A_TRIANGLE;
					break;

				case 3:
					waveform = PS5000A_DC_VOLTAGE;
					do 
					{
						printf("\nEnter offset in uV: (0 to 2000000)\n"); // Ask user to enter DC offset level;
						scanf_s("%lu", &offset);
					} while (offset < 0 || offset > 2000000);
					break;

				case 4:
					waveform = PS5000A_RAMP_UP;
					break;

				case 5:
					waveform = PS5000A_RAMP_DOWN;
					break;

				case 6:
					waveform = PS5000A_SINC;
					break;

				case 7:
					waveform = PS5000A_GAUSSIAN;
					break;

				case 8:
					waveform = PS5000A_HALF_SINE;
					break;

				default:
					waveform = PS5000A_SINE;
					break;
			}
		}

		if(waveform < 8 || (ch == 'A' && unit->sigGen == SIGGEN_AWG))				// Find out frequency if required
		{
			do 
			{
				printf("\nEnter frequency in Hz: ( >0 to 20000000)\n"); // Ask user to enter signal frequency;
				scanf_s("%lf", &frequency);
			} while (frequency <= 0 || frequency > 20000000);
		}

		if (waveformSize > 0)		
		{

			// Find the delta phase value for the frequency selected
			status = ps5000aSigGenFrequencyToPhase(unit->handle, frequency, PS5000A_SINGLE, (uint32_t) waveformSize, &deltaPhase);

			status = ps5000aSetSigGenArbitrary(	unit->handle,
				0,					// offset voltage
				pkpk,				// PkToPk in microvolts. Max = 4uV  +2v to -2V
				deltaPhase,			// start delta
				deltaPhase,			// stop delta
				0,
				0, 
				arbitraryWaveform, 
				waveformSize, 
				(PS5000A_SWEEP_TYPE)0,
				(PS5000A_EXTRA_OPERATIONS)0,
				PS5000A_SINGLE,
				0, 
				0, 
				PS5000A_SIGGEN_RISING,
				PS5000A_SIGGEN_NONE,
				0);

			printf(status?"\nps5000aSetSigGenArbitrary: Status Error 0x%x \n":"", (uint32_t)status);	// If status != 0, show the error
		} 
		else 
		{
			status = ps5000aSetSigGenBuiltInV2(unit->handle,
				offset, 
				pkpk, 
				(PS5000A_WAVE_TYPE) waveform, 
				frequency, 
				frequency, 
				0, 
				0, 
				(PS5000A_SWEEP_TYPE) 0,
				(PS5000A_EXTRA_OPERATIONS) 0,
				0, 
				0, 
				(PS5000A_SIGGEN_TRIG_TYPE) 0,
				(PS5000A_SIGGEN_TRIG_SOURCE) 0,
				0);
			
			printf(status?"\nps5000aSetSigGenBuiltIn: Status Error 0x%x \n":"", (uint32_t) status);		// If status != 0, show the error
		}
	}
}


/****************************************************************************
* collectStreamingImmediate
*  this function demonstrates how to collect a stream of data
*  from the unit (start collecting immediately)
***************************************************************************/
void collectStreamingImmediate(UNIT * unit)
{
	struct tPwq pulseWidth;
	struct tTriggerDirections directions;

	memset(&pulseWidth, 0, sizeof(struct tPwq));
	memset(&directions, 0, sizeof(struct tTriggerDirections));

	setDefaults(unit);

	printf("Collect streaming...\n");
	printf("Data is written to disk file (stream.txt)\n");
	printf("Press a key to start\n");
	_getch();

	/* Trigger disabled	*/
	setTrigger(unit, NULL, 0, NULL, 0, &directions, &pulseWidth, 0, 0, 0);

	streamDataHandler(unit, 0);
}

/****************************************************************************
* collectStreamingTriggered
*  this function demonstrates how to collect a stream of data
*  from the unit (start collecting on trigger)
***************************************************************************/
void collectStreamingTriggered(UNIT * unit)
{
	int16_t triggerVoltage = mv_to_adc(1000,	unit->channelSettings[PS5000A_CHANNEL_A].range, unit); // ChannelInfo stores ADC counts
	struct tPwq pulseWidth;

	struct tPS5000ATriggerChannelProperties sourceDetails = { triggerVoltage,
		256 * 10,
		triggerVoltage,
		256 * 10,
		PS5000A_CHANNEL_A,
		PS5000A_LEVEL };

	struct tPS5000ATriggerConditions conditions = {	PS5000A_CONDITION_TRUE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE,
		PS5000A_CONDITION_DONT_CARE};

	struct tTriggerDirections directions = { PS5000A_RISING,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE,
		PS5000A_NONE };

	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect streaming triggered...\n");
	printf("Data is written to disk file (stream.txt)\n");
	printf("Press a key to start\n");
	_getch();
	
	setDefaults(unit);

	/* Trigger enabled
	* Rising edge
	* Threshold = 1000mV */
	setTrigger(unit, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, 0, 0, 0);

	streamDataHandler(unit, 0);
}


/****************************************************************************
* displaySettings 
* Displays information about the user configurable settings in this example
* Parameters 
* - unit        pointer to the UNIT structure
*
* Returns       none
***************************************************************************/
void displaySettings(UNIT *unit)
{
	int32_t ch;
	int32_t voltage;
	PICO_STATUS status = PICO_OK;
	PS5000A_DEVICE_RESOLUTION resolution = PS5000A_DR_8BIT;

	printf("\nReadings will be scaled in (%s)\n", (scaleVoltages)? ("mV") : ("ADC counts"));
	printf("\n");

	for (ch = 0; ch < unit->channelCount; ch++)
	{
		if (!(unit->channelSettings[ch].enabled))
		{
			printf("Channel %c Voltage Range = Off\n", 'A' + ch);
		}
		else
		{
			voltage = inputRanges[unit->channelSettings[ch].range];
			printf("Channel %c Voltage Range = ", 'A' + ch);
			
			if (voltage < 1000)
			{
				printf("%dmV\n", voltage);
			}
			else
			{
				printf("%dV\n", voltage / 1000);
			}
		}
	}

	printf("\n");

	status = ps5000aGetDeviceResolution(unit->handle, &resolution);

	printf("Device Resolution: ");
	printResolution(&resolution);

}

/****************************************************************************
* openDevice 
* Parameters 
* - unit        pointer to the UNIT structure, where the handle will be stored
* - serial		pointer to the int8_t array containing serial number
*
* Returns
* - PICO_STATUS to indicate success, or if an error occurred
***************************************************************************/
PICO_STATUS openDevice(UNIT *unit, int8_t *serial)
{
	PICO_STATUS status;

	if (serial == NULL)
	{
		status = ps5000aOpenUnit(&unit->handle, NULL, PS5000A_DR_8BIT);
	}
	else
	{
		status = ps5000aOpenUnit(&unit->handle, serial, PS5000A_DR_8BIT);
	}

	unit->openStatus = (int16_t) status;
	unit->complete = 1;

	return status;
}

/****************************************************************************
* handleDevice
* Parameters
* - unit        pointer to the UNIT structure, where the handle will be stored
*
* Returns
* - PICO_STATUS to indicate success, or if an error occurred
***************************************************************************/
PICO_STATUS handleDevice(UNIT * unit)
{
	int16_t value = 0;
	int32_t i;
	struct tPwq pulseWidth;
	struct tTriggerDirections directions;
	PICO_STATUS status;

	if (unit->openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
	{
		unit->openStatus = (int16_t) changePowerSource(unit->handle, PICO_POWER_SUPPLY_NOT_CONNECTED, unit);
	}

	printf("Handle: %d\n", unit->handle);
	
	if (unit->openStatus != PICO_OK)
	{
		printf("Unable to open device\n");
		printf("Error code : 0x%08x\n", (uint32_t) unit->openStatus);
		while(!_kbhit());
		exit(99); // exit program
	}

	printf("Device opened successfully, cycle %d\n\n", ++cycles);
	// setup device info - unless it's set already
	if (unit->model == MODEL_NONE)
	{
		set_info(unit);
	}
	
	timebase = 1;

	ps5000aMaximumValue(unit->handle, &value);
	unit->maxADCValue = value;

	status = ps5000aCurrentPowerSource(unit->handle);

	for (i = 0; i < unit->channelCount; i++)
	{
		// Do not enable channels C and D if power supply not connected for PS544XA/B devices
		if(unit->channelCount == QUAD_SCOPE && status == PICO_POWER_SUPPLY_NOT_CONNECTED && i >= DUAL_SCOPE)
		{
			unit->channelSettings[i].enabled = FALSE;
		}
		else
		{
			unit->channelSettings[i].enabled = TRUE;
		}

		unit->channelSettings[i].DCcoupled = TRUE;
		unit->channelSettings[i].range = PS5000A_5V;
		unit->channelSettings[i].analogueOffset = 0.0f;
	}

	memset(&directions, 0, sizeof(struct tTriggerDirections));
	memset(&pulseWidth, 0, sizeof(struct tPwq));

	setDefaults(unit);

	/* Trigger disabled	*/
	setTrigger(unit, NULL, 0, NULL, 0, &directions, &pulseWidth, 0, 0, 0);

	return unit->openStatus;
}

/****************************************************************************
* closeDevice 
****************************************************************************/
void closeDevice(UNIT *unit)
{
	ps5000aCloseUnit(unit->handle);
}

/****************************************************************************
* mainMenu
* Controls default functions of the seelected unit
* Parameters
* - unit        pointer to the UNIT structure
*
* Returns       none
***************************************************************************/
void mainMenu(UNIT *unit)
{
	int8_t ch = '.';
	while (ch != 'X')
	{
		displaySettings(unit);

		printf("\n\n");
		printf("Please select operation:\n\n");

		printf("B - Immediate block                           V - Set voltages\n");
		printf("T - Triggered block                           I - Set timebase\n");
		printf("E - Collect a block of data using ETS         A - ADC counts/mV\n");
		printf("R - Collect set of rapid captures\n");
		printf("S - Immediate streaming\n");
		printf("W - Triggered streaming\n");

		if(unit->sigGen != SIGGEN_NONE)
		{
			printf("G - Signal generator\n");
		}
		
		printf("D - Set resolution\n");
		printf("                                              X - Exit\n");
		printf("Operation:");

		ch = toupper(_getch());

		printf("\n\n");

		switch (ch) 
		{
			case 'B':
				collectBlockImmediate(unit);
				break;

			case 'T':
				collectBlockTriggered(unit);
				break;

			case 'R':
				collectRapidBlock(unit);
				break;

			case 'S':
				collectStreamingImmediate(unit);
				break;

			case 'W':
				collectStreamingTriggered(unit);
				break;

			case 'E':
			
				if(unit->hasHardwareETS == FALSE)
				{
					printf("This model does not support hasHardwareETS\n\n");
					break;
				}

				collectBlockEts(unit);
				break;

			case 'G':
				if(unit->sigGen == SIGGEN_NONE)
				{
					printf("This model does not have a signal generator\n\n");
					break;
				}

				setSignalGenerator(unit);
				break;

			case 'V':
				setVoltages(unit);
				break;

			case 'I':
				setTimebase(unit);
				break;

			case 'A':
				scaleVoltages = !scaleVoltages;
				break;

			case 'D':
				setResolution(unit);
				break;

			case 'X':
				break;

			default:
				printf("Invalid operation\n");
				break;
		}
	}
}


/****************************************************************************
* main
*
***************************************************************************/
int32_t main(void)
{
	int8_t ch;
	uint16_t devCount = 0, listIter = 0,	openIter = 0;
	//device indexer -  64 chars - 64 is maximum number of picoscope devices handled by driver
	int8_t devChars[] =
			"1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#";
	PICO_STATUS status = PICO_OK;
	UNIT allUnits[MAX_PICO_DEVICES];

	printf("PS5000A driver example program\n");
	printf("\nEnumerating Units...\n");

	do
	{
		status = openDevice(&(allUnits[devCount]), NULL);
		
		if (status == PICO_OK || status == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			allUnits[devCount++].openStatus = (int16_t) status;
		}

	} while(status != PICO_NOT_FOUND);

	if (devCount == 0)
	{
		printf("Picoscope devices not found\n");
		return 1;
	}
	// if there is only one device, open and handle it here
	if (devCount == 1)
	{
		printf("Found one device, opening...\n\n");
		status = allUnits[0].openStatus;

		if (status == PICO_OK || status == PICO_POWER_SUPPLY_NOT_CONNECTED)
		{
			set_info(&allUnits[0]);
			status = handleDevice(&allUnits[0]);
		}

		if (status != PICO_OK)
		{
			printf("Picoscope devices open failed, error code 0x%x\n",(uint32_t)status);
			return 1;
		}

		//printf("Model\t: %7s\nS/N\t: %s\n", allUnits[0].modelString, allUnits[0].serial);
		mainMenu(&allUnits[0]);
		closeDevice(&allUnits[0]);
		printf("Exit...\n");
		return 0;
	}
	else
	{
		// more than one unit
		printf("Found %d devices, initializing...\n\n", devCount);

		for (listIter = 0; listIter < devCount; listIter++)
		{
			if (allUnits[listIter].openStatus == PICO_OK || allUnits[listIter].openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED)
			{
				set_info(&allUnits[listIter]);
				openIter++;
			}
		}
	}
	
	// None
	if (openIter == 0)
	{
		printf("Picoscope devices init failed\n");
		return 1;
	}
	// Just one - handle it here
	if (openIter == 1)
	{
		for (listIter = 0; listIter < devCount; listIter++)
		{
			if (!(allUnits[listIter].openStatus == PICO_OK || allUnits[listIter].openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED))
			{
				break;
			}
		}
		
		printf("One device opened successfully\n");
		printf("Model\t: %s\nS/N\t: %s\n", allUnits[listIter].modelString, allUnits[listIter].serial);
		status = handleDevice(&allUnits[listIter]);
		
		if (status != PICO_OK)
		{
			printf("Picoscope device open failed, error code 0x%x\n", (uint32_t)status);
			return 1;
		}
		
		mainMenu(&allUnits[listIter]);
		closeDevice(&allUnits[listIter]);
		printf("Exit...\n");
		return 0;
	}
	printf("Found %d devices, pick one to open from the list:\n", devCount);

	for (listIter = 0; listIter < devCount; listIter++)
	{
		printf("%c) Picoscope %7s S/N: %s\n", devChars[listIter],
				allUnits[listIter].modelString, allUnits[listIter].serial);
	}

	printf("ESC) Cancel\n");

	ch = '.';
	//if escape
	while (ch != 27)
	{
		ch = _getch();
		//if escape
		if (ch == 27)
			continue;
		for (listIter = 0; listIter < devCount; listIter++)
		{
			if (ch == devChars[listIter])
			{
				printf("Option %c) selected, opening Picoscope %7s S/N: %s\n",
						devChars[listIter], allUnits[listIter].modelString,
						allUnits[listIter].serial);
				
				if ((allUnits[listIter].openStatus == PICO_OK || allUnits[listIter].openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED))
				{
					status = handleDevice(&allUnits[listIter]);
				}
				
				if (status != PICO_OK)
				{
					printf("Picoscope devices open failed, error code 0x%x\n", (uint32_t)status);
					return 1;
				}

				mainMenu(&allUnits[listIter]);

				printf("Found %d devices, pick one to open from the list:\n",devCount);
				
				for (listIter = 0; listIter < devCount; listIter++)
				{
					printf("%c) Picoscope %7s S/N: %s\n", devChars[listIter],
							allUnits[listIter].modelString,
							allUnits[listIter].serial);
				}
				
				printf("ESC) Cancel\n");
			}
		}
	}

	for (listIter = 0; listIter < devCount; listIter++)
	{
		closeDevice(&allUnits[listIter]);
	}

	printf("Exit...\n");
	
	return 0;
}
