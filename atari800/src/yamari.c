/*
 * yamari.c - Emulation of the YAMari sound card.
 *
 * Copyright (C) 2019 Jerzy Kut
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

/* TODO: exclude OPL3 depending on --enable-opl3_emulation (OPL3_EMU) configuration parameter */

#define COUNT_CYCLES

#include "yamari.h"
#include "pokeysnd.h"
#include "atari.h"
#include "antic.h"
#include "util.h"
#include "statesav.h"
#include "log.h"
#include <stdlib.h>
#include <math.h>


int YAMARI_enable = FALSE;
int YAMARI_slot = YAMARI_SLOT_0;

static unsigned long main_freq;
static int bit16;
static int num_pokeys;
static int dsprate;
static double ticks_per_sample;

static double opl3_clock_freq;
static double opl3_ticks_per_sample;
static SWORD *opl3_buffer = NULL;
static unsigned int opl3_buffer_length;
static double opl3_ticks_per_tick;

#ifdef SYNCHRONIZED_SOUND
static double opl3_ticks;
#endif


static const int autochoose_order_yamari_slot[] = { 0, 1, 2, 3, 4, 5, 6, 7,
                                                 -1 };
static const int cfg_vals[] = {
	/* yamari slot */
	YAMARI_SLOT_0,
	YAMARI_SLOT_1,
	YAMARI_SLOT_2,
	YAMARI_SLOT_3,
	YAMARI_SLOT_4,
	YAMARI_SLOT_5,
	YAMARI_SLOT_6,
	YAMARI_SLOT_7
};
static const char * cfg_strings[] = {
	/* yamari slot */
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

int YAMARI_Initialise(int *argc, char *argv[])
{
	int i, j;
	int help_only = FALSE;
	/*Log_print("YAMari_Initialise");*/
	for (i = j = 1; i < *argc; i++) {
		int i_a = (i + 1 < *argc); /* is argument available? */
		int a_m = FALSE; /* error, argument missing! */
		int a_i = FALSE; /* error, argument invalid! */

		if (strcmp(argv[i], "-yamari") == 0) {
			YAMARI_enable = TRUE;
		}
		else if (strcmp(argv[i], "-noyamari") == 0) {
			YAMARI_enable = FALSE;
		}
		else if (strcmp(argv[i], "-yamari-slot") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_yamari_slot, &YAMARI_slot))
					a_i = TRUE;
			}
			else YAMARI_slot = YAMARI_SLOT_0;
		}
		else {
		 	if (strcmp(argv[i], "-help") == 0) {
		 		help_only = TRUE;
				Log_print("\t-yamari          Emulate the YAMari sound card");
				Log_print("\t-noyamari        Disable the YAMari sound card");
				Log_print("\t-yamari-slot [default|0|1|2|3|4|5|6|7]");
				Log_print("\t                 YAMari slot");
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

	if (YAMARI_enable) {
		Log_print("YAMari enabled in slot %s",
				MatchValue(autochoose_order_yamari_slot, &YAMARI_slot));
	}

	return TRUE;
}

static void yamari_initialize(unsigned long freq17, int playback_freq, int n_pokeys, int b16, YMF262_State *opl3_state)
{
	YMF262_close(YMF262_CHIP_YAMARI_INDEX);
	free(opl3_buffer);
	opl3_buffer = NULL;
	if (YAMARI_enable) {
		double samples_per_frame;
		unsigned int ticks_per_frame;
		unsigned int opl3_surplus_ticks;
		unsigned int opl3_max_ticks_per_frame;
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

		opl3_clock_freq = 14318180.0;

		/*Log_print("yamari_initialize opl3_clk: %f", opl3_clock_freq);*/
		opl3_surplus_ticks = ceil(opl3_clock_freq / playback_freq);
		opl3_max_ticks_per_frame = ticks_per_frame + opl3_surplus_ticks;
		opl3_ticks_per_sample = opl3_clock_freq / (double)dsprate;
		opl3_buffer_length = (unsigned int)ceil((double)opl3_max_ticks_per_frame / ticks_per_sample);
		opl3_ticks_per_tick = opl3_clock_freq / main_freq;
#ifdef SYNCHRONIZED_SOUND
		opl3_ticks = 0.0;
#endif /* SYNCHRONIZED_SOUND */

		YMF262_open(YMF262_CHIP_YAMARI_INDEX);
		if (opl3_state != NULL)
			YMF262_write_state(YMF262_CHIP_YAMARI_INDEX, opl3_state);
		YMF262_init(YMF262_CHIP_YAMARI_INDEX, opl3_clock_freq, playback_freq);
		opl3_buffer = Util_malloc(opl3_buffer_length * (num_pokeys == 2 ? 2 : 1) * sizeof(SWORD));
	}
}

void YAMARI_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16)
{
	YMF262_State opl3_state;
	int restore_opl3_state = YMF262_is_opened(YMF262_CHIP_YAMARI_INDEX);

	/*Log_print("YAMari_Init");*/

	if (restore_opl3_state)
		YMF262_read_state(YMF262_CHIP_YAMARI_INDEX, &opl3_state);
	yamari_initialize(freq17, playback_freq, n_pokeys, b16, restore_opl3_state ? &opl3_state : NULL);
}

void YAMARI_Exit(void)
{
	/*Log_print("YAMari_Exit");*/

	YMF262_close(YMF262_CHIP_YAMARI_INDEX);
	free(opl3_buffer);
	opl3_buffer = NULL;
}

void YAMARI_Reset(void)
{
	/*Log_print("YAMari_Reset");*/

	yamari_initialize(main_freq, dsprate, num_pokeys, bit16, NULL);
}

void YAMARI_Reinit(int playback_freq)
{
	/*Log_print("YAMari_Reinit");*/

	if (YAMARI_enable) {
		dsprate = playback_freq;
		YMF262_init(YMF262_CHIP_YAMARI_INDEX, opl3_clock_freq, playback_freq);
	}
}

int YAMARI_ReadConfig(char *string, char *ptr)
{
	/*Log_print("YAMari_ReadConfig");*/

	if (strcmp(string, "YAMARI_ENABLE") == 0) {
		int value = Util_sscanbool(ptr);
		if (value == -1)
			return FALSE;
		YAMARI_enable = value;
	}
	else if (strcmp(string, "YAMARI_SLOT") == 0) {
		if (!MatchParameter(ptr, autochoose_order_yamari_slot, &YAMARI_slot))
			return FALSE;
	}
	else return FALSE; /* no match */
	return TRUE; /* matched something */
}

void YAMARI_WriteConfig(FILE *fp)
{
	/*Log_print("YAMari_WriteConfig");*/

	fprintf(fp, "YAMARI_ENABLE=%d\n", YAMARI_enable);
	fprintf(fp, "YAMARI_SLOT=%s\n", MatchValue(autochoose_order_yamari_slot, &YAMARI_slot));
}

int YAMARI_InSlot(UWORD addr) {
	int base_address = 0xD500 + 0x20 * YAMARI_slot;
	return (YAMARI_enable)
		&& (addr >= base_address)
		&& (addr <= (base_address + 3));
}

int YAMARI_D5GetByte(UWORD addr, int no_side_effects)
{
	int result = 0xff;
	if (YAMARI_enable) {
		int base_address = 0xd500 + 0x20 * YAMARI_slot;
		if (addr == base_address) {
			double tick = opl3_ticks_per_tick * ANTIC_CPU_CLOCK;
			result = YMF262_read(YMF262_CHIP_YAMARI_INDEX, tick);
		}
	}
	return result;
}

void YAMARI_D5PutByte(UWORD addr, UBYTE byte)
{
	if (YAMARI_enable) {
		int base_address = 0xd500 + 0x20 * YAMARI_slot;
		if ((addr >= base_address) && (addr <= (base_address + 3))) {
			UWORD chip_addr = addr - base_address;
			double tick = opl3_ticks_per_tick * ANTIC_CPU_CLOCK;
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateYAMari();
#endif
			YMF262_write(YMF262_CHIP_YAMARI_INDEX, chip_addr, byte, tick);
		}
	}
}

static UBYTE* opl3_generate_samples(UBYTE *sndbuffer, int samples)
{
	int ticks;
	int count;
	unsigned int pokeys_count = num_pokeys == 2 ? 2 : 1;
	unsigned int buflen = samples > opl3_buffer_length ? opl3_buffer_length : samples;
	unsigned int amount = 0;

	if (YAMARI_enable) {
		/*Log_print("opl3_generate_samples %d", buflen);*/
		while (buflen > 0) {
			count = 0;
			ticks = buflen * opl3_ticks_per_sample;
			count = YMF262_calculate_sample(YMF262_CHIP_YAMARI_INDEX, ticks, opl3_buffer + amount, buflen);
			amount += count;
			buflen -= count;
		}
		if (amount > 0) {
			if (pokeys_count == 2) {
				Util_mix(sndbuffer, opl3_buffer, amount, 128, bit16, pokeys_count, 0, 2, 0);
				Util_mix(sndbuffer, opl3_buffer, amount, 128, bit16, pokeys_count, 1, 2, 1);
			}
			else /*(pokeys_count == 1)*/ {
				Util_mix(sndbuffer, opl3_buffer, amount, 128, bit16, pokeys_count, 0, 1, 0);
			}
		}
	}
	return sndbuffer + amount * (bit16 ? 2 : 1) * pokeys_count;
}

static UBYTE* generate_samples(UBYTE *sndbuffer, int samples)
{
	opl3_generate_samples(sndbuffer, samples);
	return sndbuffer + samples * (num_pokeys == 2 ? 2 : 1);
}

void YAMARI_Process(void *sndbuffer, int sndn)
{
	/*Log_print("YAMari_Process");*/

	if (YAMARI_enable) {
		int two_pokeys = (num_pokeys == 2);
		unsigned int sample_size = (two_pokeys ? 2 : 1);
		unsigned int samples_count = sndn / sample_size;
		generate_samples((UBYTE*)sndbuffer, samples_count);
	}
}

#ifdef SYNCHRONIZED_SOUND
#ifndef COUNT_CYCLES
unsigned int YAMARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;

	/*Log_print("YAMari_GenerateSync");*/

	if (YAMARI_enable) {
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
static unsigned int opl3_generate_sync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;
	if (YAMARI_enable) {
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
		/*Log_print("opl3_generate_sync %d", samples_count);*/
		opl3_ticks += num_ticks * opl3_ticks_per_tick;
		opl3_ticks = modf(opl3_ticks, &int_part);
		ticks = int_part;
		/*Log_print("YAMari_GenerateSync");*/
		/*Log_print("opl3_ticks=%d, num_ticks=%d", ticks, num_ticks);*/
		if (ticks > 0) {
			count = YMF262_calculate_sample(YMF262_CHIP_YAMARI_INDEX, ticks, opl3_buffer, samples_count);
		}
		/* overgenerate ticks to make lacking sample */
		while (count < samples_count) {
			opl3_ticks += opl3_ticks_per_tick;
			opl3_ticks = modf(opl3_ticks, &int_part);
			ticks = int_part;
			if (ticks > 0) {
				amount = YMF262_calculate_sample(YMF262_CHIP_YAMARI_INDEX, ticks, opl3_buffer + count, 1);
				count += amount;
			}
			overclock++;
		}
		/*unsigned int expected_ticks = samples_count * ticks_per_sample;
		Log_print("over=%d, num=%d, exp=%d, diff=%d", overclock, num_ticks, expected_ticks, num_ticks+overclock-expected_ticks);*/
		opl3_ticks -= overclock * opl3_ticks_per_tick;
		if (count > 0) {
			if (pokeys_count == 2) {
				Util_mix(buffer, opl3_buffer, count, 128, bit16, pokeys_count, 0, 2, 0);
				Util_mix(buffer, opl3_buffer, count, 128, bit16, pokeys_count, 1, 2, 1);
			}
			else /*(pokeys_count == 1)*/ {
				Util_mix(buffer, opl3_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
			}
			buffer += count * sample_size;
		}
	}
	return buffer - buffer_begin;
}

unsigned int YAMARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	opl3_generate_sync(buffer_begin, buffer_end, num_ticks, sndn);
	return sndn;
}
#endif
#endif /* SYNCHRONIZED_SOUND */

void YAMARI_StateSave(void)
{
	/*Log_print("YAMari_StateSave");*/

	StateSav_SaveINT(&YAMARI_enable, 1);
	if (YAMARI_enable) {
		YMF262_State opl3_state;

		StateSav_SaveINT(&YAMARI_slot, 1);

		YMF262_read_state(YMF262_CHIP_YAMARI_INDEX, &opl3_state);
	}
}

void YAMARI_StateRead(void)
{
	/*Log_print("YAMari_StateRead");*/

	StateSav_ReadINT(&YAMARI_enable, 1);
	if (YAMARI_enable) {
		YMF262_State opl3_state;

		StateSav_ReadINT(&YAMARI_slot, 1);

		yamari_initialize(main_freq, dsprate, num_pokeys, bit16, &opl3_state);
	}
	else
		yamari_initialize(main_freq, dsprate, num_pokeys, bit16, NULL);
}

/*
vim:ts=4:sw=4:
*/
