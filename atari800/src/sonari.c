/*
 * sonari.c - Emulation of the SONari sound card.
 *
 * Copyright (C) 2018 Jerzy Kut
 * Copyright (c) 1998-2018 Atari800 development team (see DOC/CREDITS)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

/* TODO: exclude PSG depending on --enable-psg_emulation (PSG_EMU) configuration parameter */

#define COUNT_CYCLES

#include "sonari.h"
#include "pokeysnd.h"
#include "atari.h"
#include "antic.h"
#include "util.h"
#include "statesav.h"
#include "log.h"
#include <stdlib.h>
#include <math.h>


int SONARI_version = SONARI_NO;
int SONARI_model = SONARI_CHIP_AY;
int SONARI_model2 = SONARI_CHIP_AY;
int SONARI_slot = SONARI_SLOT_3;
double SONARI_clock_freq;

static unsigned long main_freq;
static int bit16;
static int num_pokeys;
static int dsprate;
static double ticks_per_sample;

static int psg_pan;
static double psg_ticks_per_sample;
static SWORD *psg_buffer = NULL;
static SWORD *psg_buffer2 = NULL;
static unsigned int psg_buffer_length;

#ifdef SYNCHRONIZED_SOUND
static double psg_ticks_per_tick;
static double psg_ticks;
#endif

static UBYTE psg_register = 0x00;
static UBYTE psg_register2 = 0x00;


static const int autochoose_order_sonari_version[] = { 0, 1, 2,
                                                 -1 };
static const int autochoose_order_sonari_chip[] = { 3, 4, 5,
                                                 -1 };
static const int autochoose_order_sonari_slot[] = { 6, 7, 8, 9, 10, 11, 12, 13,
                                                 -1 };
static const int cfg_vals[] = {
	/* sonari version */
	SONARI_NO,
	SONARI_MONO,
	SONARI_STEREO,
	/* sonari chip */
	SONARI_CHIP_NO,
	SONARI_CHIP_AY,
	SONARI_CHIP_YM,
	/* sonari slot */
	SONARI_SLOT_0,
	SONARI_SLOT_1,
	SONARI_SLOT_2,
	SONARI_SLOT_3,
	SONARI_SLOT_4,
	SONARI_SLOT_5,
	SONARI_SLOT_6,
	SONARI_SLOT_7
};
static const char * cfg_strings[] = {
	/* sonari version */
	"NO",
	"ONE",
	"TWO",
	/* sonari chip */
	"NO",
	"AY",
	"YM",
	/* sonari slot */
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7"
};

static int MatchParameter(char const *string, int const *allowed_vals, int *ptr)
{
	do {
		if (Util_stricmp(string, cfg_strings[*allowed_vals]) == 0) {
			*ptr = cfg_vals[*allowed_vals];
			return TRUE;
		}
	} while (*++allowed_vals != -1);
	/* *string not matched to any allowed value. */
	return FALSE;
}

static const char *MatchValue(int const *allowed_vals, int *ptr)
{
	while (*allowed_vals != -1) {
		if (cfg_vals[*allowed_vals] == *ptr) {
			return cfg_strings[*allowed_vals];
		}
		allowed_vals++;
	}
	/* *ptr not matched to any allowed value. */
	return NULL;
}

int SONARI_Initialise(int *argc, char *argv[])
{
	int i, j;
	int help_only = FALSE;
	/*Log_print("SONari_Initialise");*/
	for (i = j = 1; i < *argc; i++) {
		int i_a = (i + 1 < *argc); /* is argument available? */
		int a_m = FALSE; /* error, argument missing! */
		int a_i = FALSE; /* error, argument invalid! */

		if (strcmp(argv[i], "-sonari") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_sonari_version, &SONARI_version))
					a_i = TRUE;
			}
			else SONARI_version = SONARI_STEREO;
		}
		else if (strcmp(argv[i], "-sonari-psg1") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_sonari_chip, &SONARI_model))
					a_i = TRUE;
			}
			else SONARI_model = SONARI_CHIP_NO;
		}
		else if (strcmp(argv[i], "-sonari-psg2") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_sonari_chip, &SONARI_model2))
					a_i = TRUE;
			}
			else SONARI_model2 = SONARI_CHIP_NO;
		}
		else if (strcmp(argv[i], "-sonari-slot") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_sonari_slot, &SONARI_slot))
					a_i = TRUE;
			}
			else SONARI_slot = SONARI_SLOT_3;
		}
		else {
		 	if (strcmp(argv[i], "-help") == 0) {
		 		help_only = TRUE;
				Log_print("\t-sonari [no|one|two]");
				Log_print("\t                 Emulate the SONari sound card");
				Log_print("\t-sonari-psg1 [no|ay|ym]");
				Log_print("\t                 SONari PSG chip 1");
				Log_print("\t-sonari-psg2 [no|ay|ym]");
				Log_print("\t                 SONari PSG chip 2");
				Log_print("\t-sonari-slot [default|0|1|2|3|4|5|6|7]");
				Log_print("\t                 SONari slot");
			}
			argv[j++] = argv[i];
		}

		if (a_m) {
			Log_print("Missing argument for '%s'", argv[i]);
			return FALSE;
		}
		else if (a_i) {
			Log_print("Invalid argument for '%s'", argv[--i]);
			return FALSE;
		}
	}
	*argc = j;

	if (help_only)
		return TRUE;

	if (SONARI_version != SONARI_NO) {
		Log_print("SONari %s enabled in slot %s",
				MatchValue(autochoose_order_sonari_version, &SONARI_version), 
				MatchValue(autochoose_order_sonari_slot, &SONARI_slot));
	}

	return TRUE;
}

static void sonari_initialize(unsigned long freq17, int playback_freq, int n_pokeys, int b16, AYEMU_State *psg_state, AYEMU_State *psg_state2)
{
	AYEMU_close(AYEMU_CHIP_SONARI_LEFT_INDEX);
	AYEMU_close(AYEMU_CHIP_SONARI_RIGHT_INDEX);
	free(psg_buffer);
	psg_buffer = NULL;
	free(psg_buffer2);
	psg_buffer2 = NULL;
	if (SONARI_version != SONARI_NO) {
		double samples_per_frame;
		unsigned int ticks_per_frame;
		unsigned int psg_surplus_ticks;
		unsigned int psg_max_ticks_per_frame;
		double base_clock = Atari800_tv_mode == Atari800_TV_PAL ? 1773447.0 : 1789790.0;
		main_freq = freq17;
		dsprate = playback_freq;
		num_pokeys = n_pokeys;
		bit16 = b16;

		/* calculation base is base system clock (!) tick because it's used to clock synchronized sound (taken from pokeysnd.c) */
		samples_per_frame = (double)dsprate / (Atari800_tv_mode == Atari800_TV_PAL ? Atari800_FPS_PAL : Atari800_FPS_NTSC);
		ticks_per_frame = Atari800_tv_mode*ANTIC_LINE_C;
		/* A single call to Atari800_Frame may emulate a bit more CPU ticks than the exact number of
		   ticks per frame (Atari800_tv_mode*114). So we add a few ticks to buffer size just to be safe. */
		ticks_per_sample = (double)ticks_per_frame / samples_per_frame;

		SONARI_clock_freq = base_clock;

		/*Log_print("sonari_initialize psg_clk: %f", SONARI_clock_freq);*/
		psg_pan = (num_pokeys == 2) ? AYEMU_PSG_PAN_ABC : AYEMU_PSG_PAN_MONO;
		psg_surplus_ticks = ceil(SONARI_clock_freq / playback_freq);
		psg_max_ticks_per_frame = ticks_per_frame + psg_surplus_ticks;
		psg_ticks_per_sample = SONARI_clock_freq / (double)dsprate;
		psg_buffer_length = (unsigned int)ceil((double)psg_max_ticks_per_frame / ticks_per_sample);
#ifdef SYNCHRONIZED_SOUND
		psg_ticks_per_tick = SONARI_clock_freq / main_freq;
		psg_ticks = 0.0;
#endif /* SYNCHRONIZED_SOUND */

		AYEMU_open(AYEMU_CHIP_SONARI_LEFT_INDEX);
		if (psg_state != NULL)
			AYEMU_write_state(AYEMU_CHIP_SONARI_LEFT_INDEX, psg_state);
		AYEMU_init(AYEMU_CHIP_SONARI_LEFT_INDEX, SONARI_clock_freq, SONARI_model == SONARI_CHIP_AY ? AYEMU_PSG_MODEL_AY : AYEMU_PSG_MODEL_YM, psg_pan, playback_freq);
		psg_buffer = Util_malloc(psg_buffer_length * (num_pokeys == 2 ? 2 : 1) * sizeof(SWORD));
		if (SONARI_version == SONARI_STEREO) {
			AYEMU_open(AYEMU_CHIP_SONARI_RIGHT_INDEX);
			if (psg_state2 != NULL)
				AYEMU_write_state(AYEMU_CHIP_SONARI_RIGHT_INDEX, psg_state2);
			AYEMU_init(AYEMU_CHIP_SONARI_RIGHT_INDEX, SONARI_clock_freq, SONARI_model2 == SONARI_CHIP_AY ? AYEMU_PSG_MODEL_AY : AYEMU_PSG_MODEL_YM, psg_pan, playback_freq);
			psg_buffer2 = Util_malloc(psg_buffer_length * (num_pokeys == 2 ? 2 : 1) * sizeof(SWORD));
		}
	}
}

void SONARI_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16)
{
	AYEMU_State psg_state;
	AYEMU_State psg_state2;
	int restore_psg_state = AYEMU_is_opened(AYEMU_CHIP_SONARI_LEFT_INDEX);
	int restore_psg_state2 = AYEMU_is_opened(AYEMU_CHIP_SONARI_RIGHT_INDEX);

	/*Log_print("SONari_Init");*/

	if (restore_psg_state)
		AYEMU_read_state(AYEMU_CHIP_SONARI_LEFT_INDEX, &psg_state);
	if (restore_psg_state2)
		AYEMU_read_state(AYEMU_CHIP_SONARI_RIGHT_INDEX, &psg_state2);
	sonari_initialize(freq17, playback_freq, n_pokeys, b16, restore_psg_state ? &psg_state : NULL, restore_psg_state2 ? &psg_state2 : NULL);
}

void SONARI_Exit(void)
{
	/*Log_print("SONari_Exit");*/

	AYEMU_close(AYEMU_CHIP_SONARI_LEFT_INDEX);
	AYEMU_close(AYEMU_CHIP_SONARI_RIGHT_INDEX);
	free(psg_buffer);
	psg_buffer = NULL;
	free(psg_buffer2);
	psg_buffer2 = NULL;
}

void SONARI_Reset(void)
{
	/*Log_print("SONari_Reset");*/

	if (SONARI_version != SONARI_NO) {
		psg_register = 0x00;
		psg_register2 = 0x00;
	}
	sonari_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

void SONARI_Reinit(int playback_freq)
{
	/*Log_print("SONari_Reinit");*/

	if (SONARI_version != SONARI_NO) {
		dsprate = playback_freq;
		AYEMU_init(AYEMU_CHIP_SONARI_LEFT_INDEX, SONARI_clock_freq, SONARI_model == SONARI_CHIP_AY ? AYEMU_PSG_MODEL_AY : AYEMU_PSG_MODEL_YM, psg_pan, playback_freq);
		if (SONARI_version == SONARI_STEREO)
			AYEMU_init(AYEMU_CHIP_SONARI_RIGHT_INDEX, SONARI_clock_freq, SONARI_model2 == SONARI_CHIP_AY ? AYEMU_PSG_MODEL_AY : AYEMU_PSG_MODEL_YM, psg_pan, playback_freq);
	}
}

int SONARI_ReadConfig(char *string, char *ptr)
{
	/*Log_print("SONari_ReadConfig");*/

	if (strcmp(string, "SONARI_VERSION") == 0) {
		if (!MatchParameter(ptr, autochoose_order_sonari_version, &SONARI_version))
			return FALSE;
	}
	else if (strcmp(string, "SONARI_CHIP1") == 0) {
		if (!MatchParameter(ptr, autochoose_order_sonari_chip, &SONARI_model))
			return FALSE;
	}
	else if (strcmp(string, "SONARI_CHIP2") == 0) {
		if (!MatchParameter(ptr, autochoose_order_sonari_chip, &SONARI_model2))
			return FALSE;
	}
	else if (strcmp(string, "SONARI_SLOT") == 0) {
		if (!MatchParameter(ptr, autochoose_order_sonari_slot, &SONARI_slot))
			return FALSE;
	}
	else return FALSE; /* no match */
	return TRUE; /* matched something */
}

void SONARI_WriteConfig(FILE *fp)
{
	/*Log_print("SONari_WriteConfig");*/

	fprintf(fp, "SONARI_VERSION=%s\n", MatchValue(autochoose_order_sonari_version, &SONARI_version));
	fprintf(fp, "SONARI_CHIP1=%s\n", MatchValue(autochoose_order_sonari_chip, &SONARI_model));
	fprintf(fp, "SONARI_CHIP2=%s\n", MatchValue(autochoose_order_sonari_chip, &SONARI_model2));
	fprintf(fp, "SONARI_SLOT=%s\n", MatchValue(autochoose_order_sonari_slot, &SONARI_slot));
}

int SONARI_InSlot(UWORD addr) {
	int base_address = 0xD500 + 0x20 * SONARI_slot;
	return (SONARI_version != SONARI_NO)
		&& (addr >= base_address)
		&& (addr <= (base_address + (SONARI_version == SONARI_MONO ? 1 : 3)));
}

int SONARI_D5GetByte(UWORD addr, int no_side_effects)
{
	int result = 0xff;
	if (SONARI_version != SONARI_NO) {
		int base_address = 0xd500 + 0x20 * SONARI_slot;
		if ((addr >= base_address) && (addr <= (base_address + 1))) {
			if (SONARI_model != SONARI_CHIP_NO) {
				if (addr == base_address) {
					if ((psg_register & 0x0f) <= 0x0d) {
						/* PSG read data / register select */
						result = AYEMU_read(AYEMU_CHIP_SONARI_LEFT_INDEX, psg_register & 0x0f);
					}
					else if ((psg_register & 0x0f) == 0x0e) {
						/* signature */
						result = 'S' | (SONARI_model == SONARI_CHIP_AY ? 0x80 : 0x00);
					}
					else if ((psg_register & 0x0f) == 0x0f) {
						/* signature */
						result = 'N';
					}
				}
				else if (addr == (base_address + 1)) {
					/* PSG write data / read register address */
					result = psg_register;
				}
			}
		}
		else if ( (SONARI_version == SONARI_STEREO)
				&& (addr >= base_address) && (addr <= (base_address + 3)) ) {
			if (SONARI_model2 != SONARI_CHIP_NO) {
				if (addr == (base_address + 2)) {
					if ((psg_register2 & 0x0f) <= 0x0d) {
						/* PSG read data / register select */
						result = AYEMU_read(AYEMU_CHIP_SONARI_RIGHT_INDEX, psg_register2 & 0x0f);
					}
					else if ((psg_register2 & 0x0f) == 0x0e) {
						/* signature */
						result = 'S' | (SONARI_model2 == SONARI_CHIP_AY ? 0x80 : 0x00);
					}
					else if ((psg_register2 & 0x0f) == 0x0f) {
						/* signature */
						result = 'N';
					}
				}
				else if (addr == (base_address + 3)) {
					/* PSG write data / read register address */
					result = psg_register2;
				}
			}
		}
	}
	return result;
}

void SONARI_D5PutByte(UWORD addr, UBYTE byte)
{
	if (SONARI_version != SONARI_NO) {
		int base_address = 0xd500 + 0x20 * SONARI_slot;
		if ((addr >= base_address) && (addr <= (base_address + 1))) {
			if (SONARI_model != SONARI_CHIP_NO) {
				if (addr == (base_address + 0)) {
					/* PSG read data / register select */
					psg_register = byte;
				}
				else if (addr == (base_address + 1)) {
					/* PSG write data / register address */
#ifdef SYNCHRONIZED_SOUND
					POKEYSND_UpdateSONari();
#endif
					AYEMU_write(AYEMU_CHIP_SONARI_LEFT_INDEX, psg_register & 0x0f, byte);
				}
			}
		}
		else if ( (SONARI_version == SONARI_STEREO)
				&& (addr >= base_address) && (addr <= (base_address + 3)) ) {
			if (SONARI_model2 != SONARI_CHIP_NO) {
				if (addr == (base_address + 2)) {
					/* PSG read data / register select */
					psg_register2 = byte;
				}
				else if (addr == (base_address + 3)) {
					/* PSG write data / register address */
#ifdef SYNCHRONIZED_SOUND
					POKEYSND_UpdateSONari();
#endif
					AYEMU_write(AYEMU_CHIP_SONARI_RIGHT_INDEX, psg_register2 & 0x0f, byte);
				}
			}
		}
	}
}

static UBYTE* psg_generate_samples(UBYTE *sndbuffer, int samples)
{
	int ticks;
	int count;
	unsigned int pokeys_count = num_pokeys == 2 ? 2 : 1;
	unsigned int buflen = samples > psg_buffer_length ? psg_buffer_length : samples;
	unsigned int amount = 0;

	if ( ( (SONARI_model != SONARI_CHIP_NO) || ( (SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO) ) ) ) {
		/*Log_print("psg_generate_samples %d", buflen);*/
		while (buflen > 0) {
			count = 0;
			ticks = buflen * psg_ticks_per_sample;
			if (SONARI_model != SONARI_CHIP_NO)
				count = AYEMU_calculate_sample(AYEMU_CHIP_SONARI_LEFT_INDEX, ticks, psg_buffer + amount, buflen);
			if ( (SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO) )
				count = AYEMU_calculate_sample(AYEMU_CHIP_SONARI_RIGHT_INDEX, ticks, psg_buffer2 + amount, buflen);
			amount += count;
			buflen -= count;
		}
		if (amount > 0) {
			if (pokeys_count == 2) {
				if (psg_pan == AYEMU_PSG_PAN_ABC) {
					if (SONARI_model != SONARI_CHIP_NO) {
						Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 2, 0);
						Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 1, 2, 1);
					}
					if ((SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO)) {
						Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 0, 2, 0);
						Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 1, 2, 1);
					}
				}
				else /*(psg_pan == AYEMU_PSG_PAN_MONO)*/ {
					if (SONARI_model != SONARI_CHIP_NO) {
						Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 1, 0);
						Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 1, 1, 0);
					}
					if ((SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO)) {
						Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 0, 1, 0);
						Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 1, 1, 0);
					}
				}
			}
			else /*(pokeys_count == 1)*/ { /* in this case psg_pan is always AYEMU_PSG_PAN_MONO */
				if (SONARI_model != SONARI_CHIP_NO)
					Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 1, 0);
				if ((SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO))
					Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 0, 1, 0);
			}
		}
	}
	return sndbuffer + amount * (bit16 ? 2 : 1) * pokeys_count;
}

static UBYTE* generate_samples(UBYTE *sndbuffer, int samples)
{
	psg_generate_samples(sndbuffer, samples);
	return sndbuffer + samples * (num_pokeys == 2 ? 2 : 1);
}

void SONARI_Process(void *sndbuffer, int sndn)
{
	/*Log_print("SONari_Process");*/

	if (SONARI_version != SONARI_NO) {
		int two_pokeys = (num_pokeys == 2);
		unsigned int sample_size = (two_pokeys ? 2 : 1);
		unsigned int samples_count = sndn / sample_size;
		generate_samples((UBYTE*)sndbuffer, samples_count);
	}
}

#ifdef SYNCHRONIZED_SOUND
#ifndef COUNT_CYCLES
unsigned int SONARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;

	/*Log_print("SONari_GenerateSync");*/

	if (SONARI_version != SONARI_NO) {
		int two_pokeys = (num_pokeys == 2);
		int sample_size = (bit16 ? 2 : 1)*(two_pokeys ? 2: 1);

		unsigned int max_samples_count = (buffer_end - buffer_begin) / sample_size;
		unsigned int requested_samples_count = sndn / sample_size;
		unsigned int samples_count = requested_samples_count > max_samples_count ? max_samples_count : requested_samples_count;
		if (samples_count > 0)
			buffer = generate_samples(buffer, samples_count);
	}

	return buffer - buffer_begin;
}
#else
static unsigned int psg_generate_sync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;
	if ( (SONARI_version != SONARI_NO)
		&& ( ( (SONARI_model != SONARI_CHIP_NO) || ( (SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO) ) ) ) ) {
		double int_part;
		int ticks;
		unsigned int amount;
		unsigned int count = 0;
		unsigned int overclock = 0;
		unsigned int pokeys_count = num_pokeys == 2 ? 2 : 1;
		unsigned int sample_size = (bit16 ? 2 : 1) * pokeys_count;
		unsigned int max_samples_count = (buffer_end - buffer_begin) / sample_size;
		unsigned int requested_samples_count = sndn / sample_size;
		unsigned int samples_count = requested_samples_count > max_samples_count ? max_samples_count : requested_samples_count;
		/*Log_print("psg_generate_sync %d", samples_count);*/
		psg_ticks += num_ticks * psg_ticks_per_tick;
		psg_ticks = modf(psg_ticks, &int_part);
		ticks = int_part;
		/*Log_print("SONari_GenerateSync");*/
		/*Log_print("psg_ticks=%d, num_ticks=%d", ticks, num_ticks);*/
		if (ticks > 0) {
			if (SONARI_model != SONARI_CHIP_NO)
				count = AYEMU_calculate_sample(AYEMU_CHIP_SONARI_LEFT_INDEX, ticks, psg_buffer, samples_count);
			if ( (SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO) )
				count = AYEMU_calculate_sample(AYEMU_CHIP_SONARI_RIGHT_INDEX, ticks, psg_buffer2, samples_count);
		}
		/* overgenerate ticks to make lacking sample */
		while (count < samples_count) {
			psg_ticks += psg_ticks_per_tick;
			psg_ticks = modf(psg_ticks, &int_part);
			ticks = int_part;
			if (ticks > 0) {
				amount = 0;
				if (SONARI_model != SONARI_CHIP_NO)
					amount = AYEMU_calculate_sample(AYEMU_CHIP_SONARI_LEFT_INDEX, ticks, psg_buffer + count, 1);
				if ( (SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO) )
					amount = AYEMU_calculate_sample(AYEMU_CHIP_SONARI_RIGHT_INDEX, ticks, psg_buffer2 + count, 1);
				count += amount;
			}
			overclock++;
		}
		/*unsigned int expected_ticks = samples_count * ticks_per_sample;
		Log_print("over=%d, num=%d, exp=%d, diff=%d", overclock, num_ticks, expected_ticks, num_ticks+overclock-expected_ticks);*/
		psg_ticks -= overclock * psg_ticks_per_tick;
		if (count > 0) {
			if (pokeys_count == 2) {
				if (psg_pan == AYEMU_PSG_PAN_ABC) {
					if (SONARI_model != SONARI_CHIP_NO) {
						Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 2, 0);
						Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 1, 2, 1);
					}
					if ( (SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO) ) {
						Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 0, 2, 0);
						Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 1, 2, 1);
					}
				}
				else /*(psg_pan == AYEMU_PSG_PAN_MONO)*/ {
					if (SONARI_model != SONARI_CHIP_NO) {
						Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
						Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 1, 1, 0);
					}
					if ( (SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO) ) {
						Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 0, 1, 0);
						Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 1, 1, 0);
					}
				}
			}
			else /*(pokeys_count == 1)*/ { /* in this case psg_pan is always AYEMU_PSG_PAN_MONO */
				if (SONARI_model != SONARI_CHIP_NO)
					Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
				if ( (SONARI_version == SONARI_STEREO) && (SONARI_model2 != SONARI_CHIP_NO) )
					Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 0, 1, 0);
			}
			buffer += count * sample_size;
		}
	}
	return buffer - buffer_begin;
}

unsigned int SONARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	psg_generate_sync(buffer_begin, buffer_end, num_ticks, sndn);
	return sndn;
}
#endif
#endif /* SYNCHRONIZED_SOUND */

void SONARI_StateSave(void)
{
	/*Log_print("SONari_StateSave");*/

	StateSav_SaveINT(&SONARI_version, 1);
	if (SONARI_version != SONARI_NO) {
		AYEMU_State psg_state;

		StateSav_SaveINT(&SONARI_slot, 1);

		StateSav_SaveINT(&SONARI_model, 1);
		
		AYEMU_read_state(AYEMU_CHIP_SONARI_LEFT_INDEX, &psg_state);

		StateSav_SaveINT((int*)psg_state.table, 32);
		StateSav_SaveINT((int*)&psg_state.type, 1);
		StateSav_SaveINT((int*)&psg_state.chip_freq, 1);
		StateSav_SaveINT((int*)psg_state.eq, 6);
		StateSav_SaveINT((int*)&psg_state.tone_a, 1);
		StateSav_SaveINT((int*)&psg_state.tone_b, 1);
		StateSav_SaveINT((int*)&psg_state.tone_c, 1);
		StateSav_SaveINT((int*)&psg_state.noise, 1);
		StateSav_SaveINT((int*)&psg_state.R7_tone_a, 1);
		StateSav_SaveINT((int*)&psg_state.R7_tone_b, 1);
		StateSav_SaveINT((int*)&psg_state.R7_tone_c, 1);
		StateSav_SaveINT((int*)&psg_state.R7_noise_a, 1);
		StateSav_SaveINT((int*)&psg_state.R7_noise_b, 1);
		StateSav_SaveINT((int*)&psg_state.R7_noise_c, 1);
		StateSav_SaveINT((int*)&psg_state.vol_a, 1);
		StateSav_SaveINT((int*)&psg_state.vol_b, 1);
		StateSav_SaveINT((int*)&psg_state.vol_c, 1);
		StateSav_SaveINT((int*)&psg_state.env_a, 1);
		StateSav_SaveINT((int*)&psg_state.env_b, 1);
		StateSav_SaveINT((int*)&psg_state.env_c, 1);
		StateSav_SaveINT((int*)&psg_state.env_freq, 1);
		StateSav_SaveINT((int*)&psg_state.env_style, 1);
		StateSav_SaveINT((int*)&psg_state.freq, 1);
		StateSav_SaveINT((int*)&psg_state.channels, 1);
		StateSav_SaveINT((int*)&psg_state.bpc, 1);
		StateSav_SaveINT((int*)&psg_state.magic, 1);
		StateSav_SaveINT((int*)&psg_state.default_chip_flag, 1);
		StateSav_SaveINT((int*)&psg_state.default_stereo_flag, 1);
		StateSav_SaveINT((int*)&psg_state.default_sound_format_flag, 1);
		StateSav_SaveINT((int*)&psg_state.dirty, 1);
		StateSav_SaveINT((int*)&psg_state.bit_a, 1);
		StateSav_SaveINT((int*)&psg_state.bit_b, 1);
		StateSav_SaveINT((int*)&psg_state.bit_c, 1);
		StateSav_SaveINT((int*)&psg_state.bit_n, 1);
		StateSav_SaveINT((int*)&psg_state.cnt_a, 1);
		StateSav_SaveINT((int*)&psg_state.cnt_b, 1);
		StateSav_SaveINT((int*)&psg_state.cnt_c, 1);
		StateSav_SaveINT((int*)&psg_state.cnt_n, 1);
		StateSav_SaveINT((int*)&psg_state.cnt_e, 1);
		StateSav_SaveINT((int*)&psg_state.chip_tacts_per_outcount, 1);
		StateSav_SaveINT((int*)&psg_state.amp_global, 1);
		StateSav_SaveINT((int*)psg_state.vols, 6*32);
		StateSav_SaveINT((int*)&psg_state.env_num, 1);
		StateSav_SaveINT((int*)&psg_state.env_pos, 1);
		StateSav_SaveINT((int*)&psg_state.cur_seed, 1);
		StateSav_SaveUBYTE(psg_state.regs, 14);

		StateSav_SaveUBYTE(&psg_register, 1);

		if (SONARI_version == SONARI_STEREO) {
			StateSav_SaveINT(&SONARI_model2, 1);

			AYEMU_read_state(AYEMU_CHIP_SONARI_RIGHT_INDEX, &psg_state);

			StateSav_SaveINT((int*)psg_state.table, 32);
			StateSav_SaveINT((int*)&psg_state.type, 1);
			StateSav_SaveINT((int*)&psg_state.chip_freq, 1);
			StateSav_SaveINT((int*)psg_state.eq, 6);
			StateSav_SaveINT((int*)&psg_state.tone_a, 1);
			StateSav_SaveINT((int*)&psg_state.tone_b, 1);
			StateSav_SaveINT((int*)&psg_state.tone_c, 1);
			StateSav_SaveINT((int*)&psg_state.noise, 1);
			StateSav_SaveINT((int*)&psg_state.R7_tone_a, 1);
			StateSav_SaveINT((int*)&psg_state.R7_tone_b, 1);
			StateSav_SaveINT((int*)&psg_state.R7_tone_c, 1);
			StateSav_SaveINT((int*)&psg_state.R7_noise_a, 1);
			StateSav_SaveINT((int*)&psg_state.R7_noise_b, 1);
			StateSav_SaveINT((int*)&psg_state.R7_noise_c, 1);
			StateSav_SaveINT((int*)&psg_state.vol_a, 1);
			StateSav_SaveINT((int*)&psg_state.vol_b, 1);
			StateSav_SaveINT((int*)&psg_state.vol_c, 1);
			StateSav_SaveINT((int*)&psg_state.env_a, 1);
			StateSav_SaveINT((int*)&psg_state.env_b, 1);
			StateSav_SaveINT((int*)&psg_state.env_c, 1);
			StateSav_SaveINT((int*)&psg_state.env_freq, 1);
			StateSav_SaveINT((int*)&psg_state.env_style, 1);
			StateSav_SaveINT((int*)&psg_state.freq, 1);
			StateSav_SaveINT((int*)&psg_state.channels, 1);
			StateSav_SaveINT((int*)&psg_state.bpc, 1);
			StateSav_SaveINT((int*)&psg_state.magic, 1);
			StateSav_SaveINT((int*)&psg_state.default_chip_flag, 1);
			StateSav_SaveINT((int*)&psg_state.default_stereo_flag, 1);
			StateSav_SaveINT((int*)&psg_state.default_sound_format_flag, 1);
			StateSav_SaveINT((int*)&psg_state.dirty, 1);
			StateSav_SaveINT((int*)&psg_state.bit_a, 1);
			StateSav_SaveINT((int*)&psg_state.bit_b, 1);
			StateSav_SaveINT((int*)&psg_state.bit_c, 1);
			StateSav_SaveINT((int*)&psg_state.bit_n, 1);
			StateSav_SaveINT((int*)&psg_state.cnt_a, 1);
			StateSav_SaveINT((int*)&psg_state.cnt_b, 1);
			StateSav_SaveINT((int*)&psg_state.cnt_c, 1);
			StateSav_SaveINT((int*)&psg_state.cnt_n, 1);
			StateSav_SaveINT((int*)&psg_state.cnt_e, 1);
			StateSav_SaveINT((int*)&psg_state.chip_tacts_per_outcount, 1);
			StateSav_SaveINT((int*)&psg_state.amp_global, 1);
			StateSav_SaveINT((int*)psg_state.vols, 6*32);
			StateSav_SaveINT((int*)&psg_state.env_num, 1);
			StateSav_SaveINT((int*)&psg_state.env_pos, 1);
			StateSav_SaveINT((int*)&psg_state.cur_seed, 1);
			StateSav_SaveUBYTE(psg_state.regs, 14);

			StateSav_SaveUBYTE(&psg_register2, 1);
		}
	}
}

void SONARI_StateRead(void)
{
	/*Log_print("SONari_StateRead");*/

	StateSav_ReadINT(&SONARI_version, 1);
	if (SONARI_version != SONARI_NO) {
		AYEMU_State psg_state;

		StateSav_ReadINT(&SONARI_slot, 1);

		StateSav_ReadINT(&SONARI_model, 1);

		StateSav_ReadINT((int*)psg_state.table, 32);
		StateSav_ReadINT((int*)&psg_state.type, 1);
		StateSav_ReadINT((int*)&psg_state.chip_freq, 1);
		StateSav_ReadINT((int*)psg_state.eq, 6);
		StateSav_ReadINT((int*)&psg_state.tone_a, 1);
		StateSav_ReadINT((int*)&psg_state.tone_b, 1);
		StateSav_ReadINT((int*)&psg_state.tone_c, 1);
		StateSav_ReadINT((int*)&psg_state.noise, 1);
		StateSav_ReadINT((int*)&psg_state.R7_tone_a, 1);
		StateSav_ReadINT((int*)&psg_state.R7_tone_b, 1);
		StateSav_ReadINT((int*)&psg_state.R7_tone_c, 1);
		StateSav_ReadINT((int*)&psg_state.R7_noise_a, 1);
		StateSav_ReadINT((int*)&psg_state.R7_noise_b, 1);
		StateSav_ReadINT((int*)&psg_state.R7_noise_c, 1);
		StateSav_ReadINT((int*)&psg_state.vol_a, 1);
		StateSav_ReadINT((int*)&psg_state.vol_b, 1);
		StateSav_ReadINT((int*)&psg_state.vol_c, 1);
		StateSav_ReadINT((int*)&psg_state.env_a, 1);
		StateSav_ReadINT((int*)&psg_state.env_b, 1);
		StateSav_ReadINT((int*)&psg_state.env_c, 1);
		StateSav_ReadINT((int*)&psg_state.env_freq, 1);
		StateSav_ReadINT((int*)&psg_state.env_style, 1);
		StateSav_ReadINT((int*)&psg_state.freq, 1);
		StateSav_ReadINT((int*)&psg_state.channels, 1);
		StateSav_ReadINT((int*)&psg_state.bpc, 1);
		StateSav_ReadINT((int*)&psg_state.magic, 1);
		StateSav_ReadINT((int*)&psg_state.default_chip_flag, 1);
		StateSav_ReadINT((int*)&psg_state.default_stereo_flag, 1);
		StateSav_ReadINT((int*)&psg_state.default_sound_format_flag, 1);
		StateSav_ReadINT((int*)&psg_state.dirty, 1);
		StateSav_ReadINT((int*)&psg_state.bit_a, 1);
		StateSav_ReadINT((int*)&psg_state.bit_b, 1);
		StateSav_ReadINT((int*)&psg_state.bit_c, 1);
		StateSav_ReadINT((int*)&psg_state.bit_n, 1);
		StateSav_ReadINT((int*)&psg_state.cnt_a, 1);
		StateSav_ReadINT((int*)&psg_state.cnt_b, 1);
		StateSav_ReadINT((int*)&psg_state.cnt_c, 1);
		StateSav_ReadINT((int*)&psg_state.cnt_n, 1);
		StateSav_ReadINT((int*)&psg_state.cnt_e, 1);
		StateSav_ReadINT((int*)&psg_state.chip_tacts_per_outcount, 1);
		StateSav_ReadINT((int*)&psg_state.amp_global, 1);
		StateSav_ReadINT((int*)psg_state.vols, 6*32);
		StateSav_ReadINT((int*)&psg_state.env_num, 1);
		StateSav_ReadINT((int*)&psg_state.env_pos, 1);
		StateSav_ReadINT((int*)&psg_state.cur_seed, 1);
		StateSav_ReadUBYTE(psg_state.regs, 14);

		StateSav_ReadUBYTE(&psg_register, 1);

		if (SONARI_version == SONARI_STEREO) {
			AYEMU_State psg_state2;

			StateSav_ReadINT(&SONARI_model2, 1);

			StateSav_ReadINT((int*)psg_state2.table, 32);
			StateSav_ReadINT((int*)&psg_state2.type, 1);
			StateSav_ReadINT((int*)&psg_state2.chip_freq, 1);
			StateSav_ReadINT((int*)psg_state2.eq, 6);
			StateSav_ReadINT((int*)&psg_state2.tone_a, 1);
			StateSav_ReadINT((int*)&psg_state2.tone_b, 1);
			StateSav_ReadINT((int*)&psg_state2.tone_c, 1);
			StateSav_ReadINT((int*)&psg_state2.noise, 1);
			StateSav_ReadINT((int*)&psg_state2.R7_tone_a, 1);
			StateSav_ReadINT((int*)&psg_state2.R7_tone_b, 1);
			StateSav_ReadINT((int*)&psg_state2.R7_tone_c, 1);
			StateSav_ReadINT((int*)&psg_state2.R7_noise_a, 1);
			StateSav_ReadINT((int*)&psg_state2.R7_noise_b, 1);
			StateSav_ReadINT((int*)&psg_state2.R7_noise_c, 1);
			StateSav_ReadINT((int*)&psg_state2.vol_a, 1);
			StateSav_ReadINT((int*)&psg_state2.vol_b, 1);
			StateSav_ReadINT((int*)&psg_state2.vol_c, 1);
			StateSav_ReadINT((int*)&psg_state2.env_a, 1);
			StateSav_ReadINT((int*)&psg_state2.env_b, 1);
			StateSav_ReadINT((int*)&psg_state2.env_c, 1);
			StateSav_ReadINT((int*)&psg_state2.env_freq, 1);
			StateSav_ReadINT((int*)&psg_state2.env_style, 1);
			StateSav_ReadINT((int*)&psg_state2.freq, 1);
			StateSav_ReadINT((int*)&psg_state2.channels, 1);
			StateSav_ReadINT((int*)&psg_state2.bpc, 1);
			StateSav_ReadINT((int*)&psg_state2.magic, 1);
			StateSav_ReadINT((int*)&psg_state2.default_chip_flag, 1);
			StateSav_ReadINT((int*)&psg_state2.default_stereo_flag, 1);
			StateSav_ReadINT((int*)&psg_state2.default_sound_format_flag, 1);
			StateSav_ReadINT((int*)&psg_state2.dirty, 1);
			StateSav_ReadINT((int*)&psg_state2.bit_a, 1);
			StateSav_ReadINT((int*)&psg_state2.bit_b, 1);
			StateSav_ReadINT((int*)&psg_state2.bit_c, 1);
			StateSav_ReadINT((int*)&psg_state2.bit_n, 1);
			StateSav_ReadINT((int*)&psg_state2.cnt_a, 1);
			StateSav_ReadINT((int*)&psg_state2.cnt_b, 1);
			StateSav_ReadINT((int*)&psg_state2.cnt_c, 1);
			StateSav_ReadINT((int*)&psg_state2.cnt_n, 1);
			StateSav_ReadINT((int*)&psg_state2.cnt_e, 1);
			StateSav_ReadINT((int*)&psg_state2.chip_tacts_per_outcount, 1);
			StateSav_ReadINT((int*)&psg_state2.amp_global, 1);
			StateSav_ReadINT((int*)psg_state2.vols, 6*32);
			StateSav_ReadINT((int*)&psg_state2.env_num, 1);
			StateSav_ReadINT((int*)&psg_state2.env_pos, 1);
			StateSav_ReadINT((int*)&psg_state2.cur_seed, 1);
			StateSav_ReadUBYTE(psg_state2.regs, 14);

			StateSav_ReadUBYTE(&psg_register2, 1);

			sonari_initialize(main_freq, dsprate, num_pokeys, bit16, &psg_state, &psg_state2);
		}
		else
			sonari_initialize(main_freq, dsprate, num_pokeys, bit16, &psg_state, NULL);
	}
	else
		sonari_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

/*
vim:ts=4:sw=4:
*/
