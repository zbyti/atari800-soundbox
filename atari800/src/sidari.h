#ifndef SIDARI_H_
#define SIDARI_H_

#include "config.h"
#include "atari.h"
#include "resid.h"

#define SIDARI_NO 0
#define SIDARI_MONO 1
#define SIDARI_STEREO 2

#define SIDARI_SLOT_0 0
#define SIDARI_SLOT_1 1
#define SIDARI_SLOT_2 2
#define SIDARI_SLOT_3 3
#define SIDARI_SLOT_4 4
#define SIDARI_SLOT_5 5
#define SIDARI_SLOT_6 6
#define SIDARI_SLOT_7 7

extern int SIDARI_version;
extern int SIDARI_slot;
extern double SIDARI_clock_freq;

int SIDARI_Initialise(int *argc, char *argv[]);
void SIDARI_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16);
void SIDARI_Exit(void);
void SIDARI_Reset(void);
void SIDARI_Reinit(int playback_freq);
int SIDARI_ReadConfig(char *string, char *ptr);
void SIDARI_WriteConfig(FILE *fp);
int SIDARI_InSlot(UWORD addr);
int SIDARI_D5GetByte(UWORD addr, int no_side_effects);
void SIDARI_D5PutByte(UWORD addr, UBYTE byte);
void SIDARI_Process(void *sndbuffer, int sndn);
#ifdef SYNCHRONIZED_SOUND
unsigned int SIDARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int ticks, unsigned int sndn);
#endif
void SIDARI_StateSave(void);
void SIDARI_StateRead(void);

#endif /* SIDARI_H_ */
