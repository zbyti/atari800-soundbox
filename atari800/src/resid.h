
#ifndef RESID_H_
#define RESID_H_

#include "atari.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RESID_SID_MODEL_8580 0
#define RESID_SID_MODEL_6581 1
#define RESID_SID_FILTER_LINEAR 2
#define RESID_SID_FILTER_NONE 3
#define RESID_SID_MODEL_LAST RESID_SID_FILTER_NONE

#define RESID_SYNTHESIS_METHOD_RESAMPLE_INTERPOLATE 0
#define RESID_SYNTHESIS_METHOD_RESAMPLE_FAST 1
#define RESID_SYNTHESIS_METHOD_INTERPOLATE 2
#define RESID_SYNTHESIS_METHOD_FAST 3
#define RESID_SYNTHESIS_METHOD_LAST RESID_SYNTHESIS_METHOD_FAST

#define RESID_CHIP_SLIGHTSID_INDEX 0
#define RESID_CHIP_SLIGHTSID_LEFT_INDEX 0
#define RESID_CHIP_SLIGHTSID_RIGHT_INDEX 1
#define RESID_CHIP_EVIE_INDEX 2
#define RESID_CHIP_SIDARI_INDEX 3
#define RESID_CHIP_SIDARI_LEFT_INDEX 3
#define RESID_CHIP_SIDARI_RIGHT_INDEX 4

typedef enum {
	ATTACK,
	DECAY_SUSTAIN,
	RELEASE
} RESID_Envelope;

typedef struct {
	UBYTE sid_register[0x20];

	UBYTE bus_value;
	ULONG bus_value_ttl;

	ULONG accumulator[3];
	ULONG shift_register[3];
	UWORD rate_counter[3];
	UWORD rate_counter_period[3];
	UWORD exponential_counter[3];
	UWORD exponential_counter_period[3];
	UBYTE envelope_counter[3];
	RESID_Envelope envelope_state[3];
	UBYTE hold_zero[3];
} RESID_State;

extern int RESID_resample_method;

void RESID_open(int sid_index);
void RESID_close(int sid_index);
int RESID_is_opened(int sid_index);
int RESID_init(int sid_index, double cycles_per_sec, int sid_model, double sample_rate);
UBYTE RESID_read(int sid_index, UBYTE addr);
void RESID_write(int sid_index, UBYTE addr, UBYTE byte);
void RESID_reset(int sid_index);
void RESID_input(int sid_index, int sample);
int RESID_calculate_sample(int sid_index, int delta, SWORD *buf, int nr);
void RESID_read_state(int sid_index, RESID_State *state);
void RESID_write_state(int sid_index, RESID_State *state);

int RESID_Initialise(int *argc, char *argv[]);
int RESID_ReadConfig(char *string, char *ptr);
void RESID_WriteConfig(FILE *fp);
void RESID_StateSave(void);
void RESID_StateRead(void);

#ifdef __cplusplus
}
#endif

#endif /* RESID_H_ */
