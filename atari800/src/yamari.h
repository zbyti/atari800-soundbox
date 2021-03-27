#ifndef YAMARI_H_
#define YAMARI_H_

#include "config.h"
#include "atari.h"
#include "ymf262.h"

#define YAMARI_NO 0
#define YAMARI_YES 1

#define YAMARI_SLOT_0 0
#define YAMARI_SLOT_1 1
#define YAMARI_SLOT_2 2
#define YAMARI_SLOT_3 3
#define YAMARI_SLOT_4 4
#define YAMARI_SLOT_5 5
#define YAMARI_SLOT_6 6
#define YAMARI_SLOT_7 7

extern int YAMARI_enable;
extern int YAMARI_slot;

int YAMARI_Initialise(int *argc, char *argv[]);
void YAMARI_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16);
void YAMARI_Exit(void);
void YAMARI_Reset(void);
void YAMARI_Reinit(int playback_freq);
int YAMARI_ReadConfig(char *string, char *ptr);
void YAMARI_WriteConfig(FILE *fp);
int YAMARI_InSlot(UWORD addr);
int YAMARI_D5GetByte(UWORD addr, int no_side_effects);
void YAMARI_D5PutByte(UWORD addr, UBYTE byte);
void YAMARI_Process(void *sndbuffer, int sndn);
#ifdef SYNCHRONIZED_SOUND
unsigned int YAMARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int ticks, unsigned int sndn);
#endif
void YAMARI_StateSave(void);
void YAMARI_StateRead(void);

#endif /* YAMARI_H_ */

