#ifndef EVIE_H_
#define EVIE_H_

#include "config.h"
#include "atari.h"
#include "resid.h"
#include "psgemu.h"

#define EVIE_NO 0
#define EVIE_1_0 1
#define EVIE_2_0 2

#define EVIE_COVOX_D6 0xd6
#define EVIE_COVOX_D7 0xd7

extern int EVIE_version;
extern int EVIE_covox_page;
extern double EVIE_sid_clock_freq;
extern double EVIE_psg_clock_freq;

int EVIE_Initialise(int *argc, char *argv[]);
void EVIE_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16);
void EVIE_Exit(void);
void EVIE_Reset(void);
void EVIE_Reinit(int playback_freq);
int EVIE_ReadConfig(char *string, char *ptr);
void EVIE_WriteConfig(FILE *fp);
int EVIE_D2GetByte(UWORD addr, int no_side_effects);
void EVIE_D2PutByte(UWORD addr, UBYTE byte);
int EVIE_D5GetByte(UWORD addr, int no_side_effects);
void EVIE_D5PutByte(UWORD addr, UBYTE byte);
void EVIE_D67PutByte(UWORD addr, UBYTE byte);
void EVIE_Process(void *sndbuffer, int sndn);
#ifdef SYNCHRONIZED_SOUND
unsigned int EVIE_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int ticks, unsigned int sndn);
#endif
void EVIE_StateSave(void);
void EVIE_StateRead(void);

#endif /* EVIE_H_ */
