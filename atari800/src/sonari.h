#ifndef SONARI_H_
#define SONARI_H_

#include "config.h"
#include "atari.h"
#include "psgemu.h"

#define SONARI_NO 0
#define SONARI_MONO 1
#define SONARI_STEREO 2

#define SONARI_CHIP_NO 0
#define SONARI_CHIP_AY 1
#define SONARI_CHIP_YM 2

#define SONARI_SLOT_0 0
#define SONARI_SLOT_1 1
#define SONARI_SLOT_2 2
#define SONARI_SLOT_3 3
#define SONARI_SLOT_4 4
#define SONARI_SLOT_5 5
#define SONARI_SLOT_6 6
#define SONARI_SLOT_7 7

extern int SONARI_version;
extern int SONARI_model;
extern int SONARI_model2;
extern int SONARI_slot;
extern double SONARI_clock_freq;

int SONARI_Initialise(int *argc, char *argv[]);
void SONARI_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16);
void SONARI_Exit(void);
void SONARI_Reset(void);
void SONARI_Reinit(int playback_freq);
int SONARI_ReadConfig(char *string, char *ptr);
void SONARI_WriteConfig(FILE *fp);
int SONARI_InSlot(UWORD addr);
int SONARI_D5GetByte(UWORD addr, int no_side_effects);
void SONARI_D5PutByte(UWORD addr, UBYTE byte);
void SONARI_Process(void *sndbuffer, int sndn);
#ifdef SYNCHRONIZED_SOUND
unsigned int SONARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int ticks, unsigned int sndn);
#endif
void SONARI_StateSave(void);
void SONARI_StateRead(void);

#endif /* SONARI_H_ */
