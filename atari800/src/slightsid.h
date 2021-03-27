#ifndef SLIGHTSID_H_
#define SLIGHTSID_H_

#include "config.h"
#include "atari.h"
#include "resid.h"

#define SLIGHTSID_NO 0
#define SLIGHTSID_MONO 1
#define SLIGHTSID_STEREO 2

extern int SLIGHTSID_version;
extern double SLIGHTSID_clock_freq;

int SLIGHTSID_Initialise(int *argc, char *argv[]);
void SLIGHTSID_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16);
void SLIGHTSID_Exit(void);
void SLIGHTSID_Reset(void);
void SLIGHTSID_Reinit(int playback_freq);
int SLIGHTSID_ReadConfig(char *string, char *ptr);
void SLIGHTSID_WriteConfig(FILE *fp);
int SLIGHTSID_D5GetByte(UWORD addr, int no_side_effects);
void SLIGHTSID_D5PutByte(UWORD addr, UBYTE byte);
void SLIGHTSID_Process(void *sndbuffer, int sndn);
#ifdef SYNCHRONIZED_SOUND
unsigned int SLIGHTSID_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int ticks, unsigned int sndn);
#endif
void SLIGHTSID_StateSave(void);
void SLIGHTSID_StateRead(void);

#endif /* SLIGHTSID_H_ */
