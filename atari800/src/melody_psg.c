/*
 * melody_psg.c - Emulation of the Melody PSG sound card.
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

/* TODO:
 * 1. exclude PSG depending on --enable-psg_emulation (PSG_EMU) configuration parameter
 */

#define COUNT_CYCLES

#include "melody_psg.h"
#include "pokeysnd.h"
#include "atari.h"
#include "antic.h"
#include "util.h"
#include "statesav.h"
#include "log.h"
#include <stdlib.h>
#include <math.h>


int MELODY_PSG_enable = FALSE;
int MELODY_PSG_model = MELODY_PSG_CHIP_AY;
int MELODY_PSG_model2 = MELODY_PSG_CHIP_AY;

double MELODY_PSG_clock_freq;

UBYTE chip_base_addr = 0x00;
UBYTE device_index = 0x00;

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

static UBYTE config = 0x00;
static int reset = FALSE;
static int mhz2 = FALSE;
static int div2 = FALSE;
static int gnd = FALSE;
static int scl = 0;
static int sda = 0;


static const int autochoose_order_melody_chip[] = { 0, 1, 2, 
                                                 -1 };
static const int cfg_vals[] = {
	/* melody chip */
	MELODY_PSG_CHIP_NO,
	MELODY_PSG_CHIP_AY,
	MELODY_PSG_CHIP_YM
};
static const char * cfg_strings[] = {
	/* melody chip */
	"NO",
	"AY",
	"YM"
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

int MELODY_PSG_Initialise(int *argc, char *argv[])
{
	int i, j;
	int help_only = FALSE;
	/*Log_print("Melody_PSG_Initialise");*/
	for (i = j = 1; i < *argc; i++) {
		int i_a = (i + 1 < *argc); /* is argument available? */
		int a_m = FALSE; /* error, argument missing! */
		int a_i = FALSE; /* error, argument invalid! */

		if (strcmp(argv[i], "-melody-psg") == 0) {
			MELODY_PSG_enable = TRUE;
		}
		else if (strcmp(argv[i], "-no-melody-psg") == 0) {
			MELODY_PSG_enable = FALSE;
		}
		else if (strcmp(argv[i], "-melody-psg1") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_melody_chip, &MELODY_PSG_model))
					a_i = TRUE;
			}
			else MELODY_PSG_model = MELODY_PSG_CHIP_NO;
		}
		else if (strcmp(argv[i], "-melody-psg2") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_melody_chip, &MELODY_PSG_model2))
					a_i = TRUE;
			}
			else MELODY_PSG_model2 = MELODY_PSG_CHIP_NO;
		}
		else {
		 	if (strcmp(argv[i], "-help") == 0) {
		 		help_only = TRUE;
				Log_print("\t-melody-psg      Emulate the Melody PSG sound card");
				Log_print("\t-no-melody-psg   Disable the Melody PSG sound card");
				Log_print("\t-melody-psg1 [no|ay|ym]");
				Log_print("\t                 Melody PSG chip 1");
				Log_print("\t-melody-psg2 [no|ay|ym]");
				Log_print("\t                 Melody PSG chip 2");
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

	if (MELODY_PSG_enable) {
		Log_print("Melody PSG enabled");
	}

	return TRUE;
}

static void melody_initialize(unsigned long freq17, int playback_freq, int n_pokeys, int b16, AYEMU_State *psg_state, AYEMU_State *psg_state2)
{
	AYEMU_close(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX);
	AYEMU_close(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX);
	free(psg_buffer);
	psg_buffer = NULL;
	free(psg_buffer2);
	psg_buffer2 = NULL;
	if (MELODY_PSG_enable) {
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

		MELODY_PSG_clock_freq = base_clock;

		/*Log_print("melody_initialize psg_clk: %f", MELODY_PSG_clock_freq);*/
		psg_pan = (num_pokeys == 2) ? AYEMU_PSG_PAN_ABC : AYEMU_PSG_PAN_MONO;
		psg_surplus_ticks = ceil(MELODY_PSG_clock_freq / playback_freq);
		psg_max_ticks_per_frame = ticks_per_frame + psg_surplus_ticks;
		psg_ticks_per_sample = MELODY_PSG_clock_freq / (double)dsprate;
		psg_buffer_length = (unsigned int)ceil((double)psg_max_ticks_per_frame / ticks_per_sample);
#ifdef SYNCHRONIZED_SOUND
		psg_ticks_per_tick = MELODY_PSG_clock_freq / main_freq;
		psg_ticks = 0.0;
#endif /* SYNCHRONIZED_SOUND */

		AYEMU_open(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX);
		if (psg_state != NULL)
			AYEMU_write_state(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, psg_state);
		AYEMU_init(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, MELODY_PSG_clock_freq, MELODY_PSG_model == MELODY_PSG_CHIP_AY ? AYEMU_PSG_MODEL_AY : AYEMU_PSG_MODEL_YM, psg_pan, playback_freq);
		psg_buffer = Util_malloc(psg_buffer_length * (num_pokeys == 2 ? 2 : 1) * sizeof(SWORD));

		AYEMU_open(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX);
		if (psg_state2 != NULL)
			AYEMU_write_state(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, psg_state2);
		AYEMU_init(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, MELODY_PSG_clock_freq, MELODY_PSG_model2 == MELODY_PSG_CHIP_AY ? AYEMU_PSG_MODEL_AY : AYEMU_PSG_MODEL_YM, psg_pan, playback_freq);
		psg_buffer2 = Util_malloc(psg_buffer_length * (num_pokeys == 2 ? 2 : 1) * sizeof(SWORD));
	}
}

void MELODY_PSG_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16)
{
	AYEMU_State psg_state;
	AYEMU_State psg_state2;
	int restore_psg_state = AYEMU_is_opened(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX);
	int restore_psg_state2 = AYEMU_is_opened(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX);

	/*Log_print("Melody_Init");*/

	if (restore_psg_state)
		AYEMU_read_state(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, &psg_state);
	if (restore_psg_state2)
		AYEMU_read_state(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, &psg_state2);
	melody_initialize(freq17, playback_freq, n_pokeys, b16, restore_psg_state ? &psg_state : NULL, restore_psg_state2 ? &psg_state2 : NULL);
}

void MELODY_PSG_Exit(void)
{
	/*Log_print("Melody_Exit");*/

	AYEMU_close(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX);
	AYEMU_close(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX);
	free(psg_buffer);
	psg_buffer = NULL;
	free(psg_buffer2);
	psg_buffer2 = NULL;
}

static void update_config(UBYTE byte)
{
	config = byte;

	reset = (byte & 0x80) != 0;
	mhz2 = (byte & 0x40) != 0;
	div2 = (byte & 0x20) != 0;
	gnd = (byte & 0x04) != 0;

	scl = (byte & 0x02) != 0;
	sda = (byte & 0x01) != 0;
	/* TODO: implement I2C and MCP4651 emulation */
}

void MELODY_PSG_Reset(void)
{
	/*Log_print("Melody_Reset");*/

	if (MELODY_PSG_enable) {
		chip_base_addr = 0x00;
		update_config(0x00);
		psg_register = 0x00;
		psg_register2 = 0x00;
	}
	melody_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

void MELODY_PSG_Reinit(int playback_freq)
{
	/*Log_print("Melody_Reinit");*/

	if (MELODY_PSG_enable) {
		dsprate = playback_freq;
		AYEMU_init(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, MELODY_PSG_clock_freq, MELODY_PSG_model == MELODY_PSG_CHIP_AY ? AYEMU_PSG_MODEL_AY : AYEMU_PSG_MODEL_YM, psg_pan, playback_freq);
		AYEMU_init(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, MELODY_PSG_clock_freq, MELODY_PSG_model2 == MELODY_PSG_CHIP_AY ? AYEMU_PSG_MODEL_AY : AYEMU_PSG_MODEL_YM, psg_pan, playback_freq);
	}
}

int MELODY_PSG_ReadConfig(char *string, char *ptr)
{
	/*Log_print("Melody_ReadConfig");*/

	if (strcmp(string, "MELODY_PSG_ENABLE") == 0) {
		int value = Util_sscanbool(ptr);
		if (value == -1)
			return FALSE;
		MELODY_PSG_enable = value;
	}
	else if (strcmp(string, "MELODY_PSG_CHIP1") == 0) {
		if (!MatchParameter(ptr, autochoose_order_melody_chip, &MELODY_PSG_model))
			return FALSE;
	}
	else if (strcmp(string, "MELODY_PSG_CHIP2") == 0) {
		if (!MatchParameter(ptr, autochoose_order_melody_chip, &MELODY_PSG_model2))
			return FALSE;
	}
	else return FALSE; /* no match */
	return TRUE; /* matched something */
}

void MELODY_PSG_WriteConfig(FILE *fp)
{
	/*Log_print("Melody_WriteConfig");*/

	fprintf(fp, "MELODY_PSG_ENABLE=%d\n", MELODY_PSG_enable);
	fprintf(fp, "MELODY_PSG_CHIP1=%s\n", MatchValue(autochoose_order_melody_chip, &MELODY_PSG_model));
	fprintf(fp, "MELODY_PSG_CHIP2=%s\n", MatchValue(autochoose_order_melody_chip, &MELODY_PSG_model2));
}

int MELODY_PSG_InSlot(UWORD addr) {
	int base_address = 0xd500;
	return (MELODY_PSG_enable && ( 
		( (addr >= (base_address + chip_base_addr))
			&& (addr <= (base_address + chip_base_addr + 3)) )
		||
		( (addr >= (base_address + 0xd8)) && (addr <= (base_address + 0xdf)) ) ));
}

static const char psg_signature[] = { 'P', 'S', 'G', 0x02 };

int MELODY_PSG_D5GetByte(UWORD addr, int no_side_effects)
{
	int result = 0xff;
	if (MELODY_PSG_enable) {
		int base_address = 0xd500;
		if (device_index == MELODY_PSG_DEVICE) {
			if ((addr >= (base_address + chip_base_addr)) && (addr <= (base_address + chip_base_addr + 3))) {
				if (MELODY_PSG_model != MELODY_PSG_CHIP_NO) {
					if (addr == (base_address + chip_base_addr)) {
						/* PSG read data / register select */
						result = AYEMU_read(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, psg_register & 0x0f);
					}
					else if (addr == (base_address + chip_base_addr + 1)) {
						/* PSG write data / read register address */
						result = psg_register;
					}
				}
				if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO) {
					if (addr == (base_address + chip_base_addr +  2)) {
						/* PSG read data / register select */
						result = AYEMU_read(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, psg_register2 & 0x0f);
					}
					else if (addr == (base_address + chip_base_addr + 3)) {
						/* PSG write data / read register address */
						result = psg_register2;
					}
				}
			}
			else if ((addr >= (base_address + 0xd8)) && (addr <= (base_address + 0xdb))) {
				result = psg_signature[addr - (base_address + 0xd8)];
			}
		}
		if (addr == (base_address + 0xdf)) {
			result = 'M';
		}
	}
	return result;
}

void MELODY_PSG_D5PutByte(UWORD addr, UBYTE byte)
{
	if (MELODY_PSG_enable) {
		int base_address = 0xd500;
		if (device_index == MELODY_PSG_DEVICE) {
			if ((addr >= (base_address + chip_base_addr)) && (addr <= (base_address + chip_base_addr + 3))
				&& (!reset)) {

				if (MELODY_PSG_model != MELODY_PSG_CHIP_NO) {
					if (addr == (base_address + chip_base_addr)) {
						/* PSG read data / register select */
						psg_register = byte;
					}
					else if (addr == (base_address + chip_base_addr + 1)) {
						/* PSG write data / register address */
#ifdef SYNCHRONIZED_SOUND
						POKEYSND_UpdateMelody();
#endif
						AYEMU_write(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, psg_register & 0x0f, byte);
					}
				}
				if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO) {
					if (addr == (base_address + chip_base_addr + 2)) {
						/* PSG read data / register select */
						psg_register2 = byte;
					}
					else if (addr == (base_address + chip_base_addr + 3)) {
						/* PSG write data / register address */
#ifdef SYNCHRONIZED_SOUND
						POKEYSND_UpdateMelody();
#endif
						AYEMU_write(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, psg_register2 & 0x0f, byte);
					}
				}
			}
			else if (addr == (base_address + 0xdc)) {
				/* base address for chip registers */
				chip_base_addr = byte & 0xe0;
			}
			else if (addr == (base_address + 0xdd)) {
				/* configuration register */
				AYEMU_State psg_state;
				AYEMU_State psg_state2;
#ifdef SYNCHRONIZED_SOUND
				POKEYSND_UpdateMelody();
#endif
				AYEMU_read_state(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, &psg_state);
				AYEMU_read_state(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, &psg_state2);
				update_config(byte);
				melody_initialize(main_freq, dsprate, num_pokeys, bit16, &psg_state, &psg_state2);
			}
		}
		if (addr == (base_address + 0xdf)) {
			device_index = byte;
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

	if (( (MELODY_PSG_model != MELODY_PSG_CHIP_NO) || (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO) ) && (!reset)) {
		/*Log_print("psg_generate_samples %d", buflen);*/
		while (buflen > 0) {
			count = 0;
			ticks = buflen * psg_ticks_per_sample;
			if (MELODY_PSG_model != MELODY_PSG_CHIP_NO)
				count = AYEMU_calculate_sample(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, ticks, psg_buffer + amount, buflen);
			if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO)
				count = AYEMU_calculate_sample(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, ticks, psg_buffer2 + amount, buflen);
			amount += count;
			buflen -= count;
		}
		if (amount > 0) {
			if (pokeys_count == 2) {
				if (psg_pan == AYEMU_PSG_PAN_ABC) {
					if (MELODY_PSG_model != MELODY_PSG_CHIP_NO) {
						Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 2, 0);
						Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 1, 2, 1);
					}
					if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO) {
						Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 0, 2, 0);
						Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 1, 2, 1);
					}
				}
				else /*(psg_pan == AYEMU_PSG_PAN_MONO)*/ {
					if (MELODY_PSG_model != MELODY_PSG_CHIP_NO) {
						Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 1, 0);
						Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 1, 1, 0);
					}
					if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO) {
						Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 0, 1, 0);
						Util_mix(sndbuffer, psg_buffer2, amount, 128, bit16, pokeys_count, 1, 1, 0);
					}
				}
			}
			else /*(pokeys_count == 1)*/ { /* in this case psg_pan is always AYEMU_PSG_PAN_MONO */
				if (MELODY_PSG_model != MELODY_PSG_CHIP_NO)
					Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 1, 0);
				if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO)
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

void MELODY_PSG_Process(void *sndbuffer, int sndn)
{
	/*Log_print("Melody_Process");*/

	if (MELODY_PSG_enable) {
		int two_pokeys = (num_pokeys == 2);
		unsigned int sample_size = (two_pokeys ? 2 : 1);
		unsigned int samples_count = sndn / sample_size;
		generate_samples((UBYTE*)sndbuffer, samples_count);
	}
}

#ifdef SYNCHRONIZED_SOUND
#ifndef COUNT_CYCLES
unsigned int MELODY_PSG_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;

	/*Log_print("Melody_GenerateSync");*/

	if (MELODY_PSG_enable && (!reset)) {
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
	if ( MELODY_PSG_enable
		&& ( ( (MELODY_PSG_model != MELODY_PSG_CHIP_NO) || (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO) ) ) ) {
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
		/*Log_print("Melody_GenerateSync");*/
		/*Log_print("psg_ticks=%d, num_ticks=%d", ticks, num_ticks);*/
		if (ticks > 0) {
			if (MELODY_PSG_model != MELODY_PSG_CHIP_NO)
				count = AYEMU_calculate_sample(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, ticks, psg_buffer, samples_count);
			if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO)
				count = AYEMU_calculate_sample(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, ticks, psg_buffer2, samples_count);
		}
		/* overgenerate ticks to make lacking sample */
		while (count < samples_count) {
			psg_ticks += psg_ticks_per_tick;
			psg_ticks = modf(psg_ticks, &int_part);
			ticks = int_part;
			if (ticks > 0) {
				amount = 0;
				if (MELODY_PSG_model != MELODY_PSG_CHIP_NO)
					amount = AYEMU_calculate_sample(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, ticks, psg_buffer + count, 1);
				if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO)
					amount = AYEMU_calculate_sample(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, ticks, psg_buffer2 + count, 1);
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
					if (MELODY_PSG_model != MELODY_PSG_CHIP_NO) {
						Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 2, 0);
						Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 1, 2, 1);
					}
					if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO) {
						Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 0, 2, 0);
						Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 1, 2, 1);
					}
				}
				else /*(psg_pan == AYEMU_PSG_PAN_MONO)*/ {
					if (MELODY_PSG_model != MELODY_PSG_CHIP_NO) {
						Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
						Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 1, 1, 0);
					}
					if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO) {
						Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 0, 1, 0);
						Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 1, 1, 0);
					}
				}
			}
			else /*(pokeys_count == 1)*/ { /* in this case psg_pan is always AYEMU_PSG_PAN_MONO */
				if (MELODY_PSG_model != MELODY_PSG_CHIP_NO)
					Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
				if (MELODY_PSG_model2 != MELODY_PSG_CHIP_NO)
					Util_mix(buffer, psg_buffer2, count, 128, bit16, pokeys_count, 0, 1, 0);
			}
			buffer += count * sample_size;
		}
	}
	return buffer - buffer_begin;
}

unsigned int MELODY_PSG_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	psg_generate_sync(buffer_begin, buffer_end, num_ticks, sndn);
	return sndn;
}
#endif
#endif /* SYNCHRONIZED_SOUND */

void MELODY_PSG_StateSave(void)
{
	/*Log_print("Melody_StateSave");*/

	StateSav_SaveINT(&MELODY_PSG_enable, 1);
	if (MELODY_PSG_enable) {
		AYEMU_State psg_state;

		StateSav_SaveINT(&MELODY_PSG_model, 1);
		
		AYEMU_read_state(AYEMU_CHIP_MELODY_PSG_LEFT_INDEX, &psg_state);

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

		StateSav_SaveINT(&MELODY_PSG_model2, 1);

		AYEMU_read_state(AYEMU_CHIP_MELODY_PSG_RIGHT_INDEX, &psg_state);

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

		StateSav_SaveUBYTE(&chip_base_addr, 1);
		StateSav_SaveUBYTE(&config, 1);
	}
}

void MELODY_PSG_StateRead(void)
{
	/*Log_print("Melody_StateRead");*/

	StateSav_ReadINT(&MELODY_PSG_enable, 1);
	if (MELODY_PSG_enable) {
		AYEMU_State psg_state;
		AYEMU_State psg_state2;

		StateSav_ReadINT(&MELODY_PSG_model, 1);

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

		StateSav_ReadINT(&MELODY_PSG_model2, 1);

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

		StateSav_ReadUBYTE(&chip_base_addr, 1);
		StateSav_ReadUBYTE(&config, 1);
		update_config(config);

		melody_initialize(main_freq, dsprate, num_pokeys, bit16, &psg_state, &psg_state2);
	}
	else
		melody_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

/*
vim:ts=4:sw=4:
*/
