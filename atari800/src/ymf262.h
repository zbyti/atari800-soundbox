
#ifndef YMF262_H_
#define YMF262_H_

#include "atari.h"
#include "opl.h"


#define YMF262_CHIP_YAMARI_INDEX 0


typedef struct
{
  unsigned char regs[0x200];
} YMF262_State;

void YMF262_open(int opl3_index);
void YMF262_close(int opl3_index);
int YMF262_is_opened(int opl3_index);
void YMF262_init(int opl3_index, double cycles_per_sec, double sample_rate);
UBYTE YMF262_read(int opl3_index, double tick);
void YMF262_write(int opl3_index, UWORD addr, UBYTE byte, double tick);
void YMF262_reset(int opl3_index);
int YMF262_calculate_sample(int opl3_index, int delta, SWORD *buf, int nr);
void YMF262_read_state(int opl3_index, YMF262_State *state);
void YMF262_write_state(int opl3_index, YMF262_State *state);

#endif /* YMF262_H_ */

