
#ifndef AYEMU_H_
#define AYEMU_H_

#include "atari.h"


#define AYEMU_PSG_MODEL_AY 0
#define AYEMU_PSG_MODEL_YM 1
#define AYEMU_PSG_MODEL_LAST AYEMU_PSG_MODEL_YM

#define AYEMU_PSG_PAN_MONO 0
#define AYEMU_PSG_PAN_ABC 1
#define AYEMU_PSG_PAN_ACB 2
#define AYEMU_PSG_PAN_LAST AYEMU_PSG_PAN_STEREO_ACB

#define AYEMU_CHIP_EVIE_INDEX 0
#define AYEMU_CHIP_SONARI_LEFT_INDEX 1
#define AYEMU_CHIP_SONARI_RIGHT_INDEX 2
#define AYEMU_CHIP_MELODY_PSG_LEFT_INDEX 3
#define AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX 4


typedef struct
{
  int table[32];		/**< table of volumes for chip */
  int type;			/**< general chip type (\b AYEMU_AY or \b AYEMU_YM) */
  int chip_freq;			/**< chip emulator frequency */
  int eq[6];			/**< volumes for channels.
				   Array contains 6 elements: 
				   A left, A right, B left, B right, C left and C right;
				   range -100...100 */

  int tone_a;           /**< R0, R1 */
  int tone_b;		/**< R2, R3 */	
  int tone_c;		/**< R4, R5 */
  int noise;		/**< R6 */
  int R7_tone_a;	/**< R7 bit 0 */
  int R7_tone_b;	/**< R7 bit 1 */
  int R7_tone_c;	/**< R7 bit 2 */
  int R7_noise_a;	/**< R7 bit 3 */
  int R7_noise_b;	/**< R7 bit 4 */
  int R7_noise_c;	/**< R7 bit 5 */
  int vol_a;		/**< R8 bits 3-0 */
  int vol_b;		/**< R9 bits 3-0 */
  int vol_c;		/**< R10 bits 3-0 */
  int env_a;		/**< R8 bit 4 */
  int env_b;		/**< R9 bit 4 */
  int env_c;		/**< R10 bit 4 */
  int env_freq;		/**< R11, R12 */
  int env_style;	/**< R13 */

  int freq;			/**< sound freq */
  int channels;			/**< channels (1-mono, 2-stereo) */
  int bpc;			/**< bits (8 or 16) */

  int magic;			/**< structure initialized flag */
  int default_chip_flag;	/**< =1 after init, resets in #ayemu_set_chip_type() */
  int default_stereo_flag;	/**< =1 after init, resets in #ayemu_set_stereo() */
  int default_sound_format_flag; /**< =1 after init, resets in #ayemu_set_sound_format() */
  int dirty;			/**< dirty flag. Sets if any emulator properties changed */

  int bit_a;			/**< state of channel A generator */
  int bit_b;			/**< state of channel B generator */
  int bit_c;			/**< state of channel C generator */
  int bit_n;			/**< current generator state */
  int cnt_a;			/**< back counter of A */
  int cnt_b;			/**< back counter of B */
  int cnt_c;			/**< back counter of C */
  int cnt_n;			/**< back counter of noise generator */
  int cnt_e;			/**< back counter of envelop generator */
  int chip_tacts_per_outcount;   /**< chip's counts per one sound signal count */
  int amp_global;		/**< scale factor for amplitude */
  int vols[6][32];              /**< stereo type (channel volumes) and chip table.
				   This cache calculated by #table and #eq  */
  int env_num;		        /**< number of current envilopment (0...15) */
  int env_pos;			/**< current position in envelop (0...127) */
  int cur_seed;		        /**< random numbers counter */

  unsigned char regs[14];
} AYEMU_State;

void AYEMU_open(int psg_index);
void AYEMU_close(int psg_index);
int AYEMU_is_opened(int psg_index);
void AYEMU_init(int psg_index, double cycles_per_sec, int psg_model, int psg_pan, double sample_rate);
UBYTE AYEMU_read(int psg_index, UBYTE addr);
void AYEMU_write(int psg_index, UBYTE addr, UBYTE byte);
void AYEMU_reset(int psg_index);
int AYEMU_calculate_sample(int psg_index, int delta, SWORD *buf, int nr);
void AYEMU_read_state(int psg_index, AYEMU_State *state);
void AYEMU_write_state(int psg_index, AYEMU_State *state);

#endif /* AYEMU_H_ */
