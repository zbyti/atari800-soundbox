#ifndef MELODY_PSG_H_
#define MELODY_PSG_H_

#include "config.h"
#include "atari.h"
#include "psgemu.h"

#define MELODY_PSG_DEVICE 0x50

#define MELODY_PSG_CHIP_NO 0
#define MELODY_PSG_CHIP_AY 1
#define MELODY_PSG_CHIP_YM 2

extern int MELODY_PSG_enable;
extern int MELODY_PSG_model;
extern int MELODY_PSG_model2;
extern double MELODY_PSG_clock_freq;
extern UBYTE chip_base_addr;
extern UBYTE device_index;


int MELODY_PSG_Initialise(int *argc, char *argv[]);
void MELODY_PSG_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16);
void MELODY_PSG_Exit(void);
void MELODY_PSG_Reset(void);
void MELODY_PSG_Reinit(int playback_freq);
int MELODY_PSG_ReadConfig(char *string, char *ptr);
void MELODY_PSG_WriteConfig(FILE *fp);
int MELODY_PSG_InSlot(UWORD addr);
int MELODY_PSG_D5GetByte(UWORD addr, int no_side_effects);
void MELODY_PSG_D5PutByte(UWORD addr, UBYTE byte);
void MELODY_PSG_Process(void *sndbuffer, int sndn);
#ifdef SYNCHRONIZED_SOUND
unsigned int MELODY_PSG_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int ticks, unsigned int sndn);
#endif
void MELODY_PSG_StateSave(void);
void MELODY_PSG_StateRead(void);

#endif /* MELODY_PSG_H_ */
