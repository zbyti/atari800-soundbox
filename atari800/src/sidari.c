/*
 * sidari.c - Emulation of the SIDari sound card.
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

#define COUNT_CYCLES

#include "sidari.h"
#include "pokeysnd.h"
#include "atari.h"
#include "antic.h"
#include "util.h"
#include "statesav.h"
#include "log.h"
#include <stdlib.h>
#include <math.h>


int SIDARI_version = SIDARI_NO;
int SIDARI_slot = SIDARI_SLOT_4;

/* Original Commodore 64 SID chip is clocked by 985248 Hz in PAL and 1022730 Hz in NTSC */
double SIDARI_clock_freq;

static int sid_model = RESID_SID_MODEL_8580;
static unsigned long main_freq;
static int bit16;
static int num_pokeys;
static int dsprate;
static double ticks_per_sample;
static double sid_ticks_per_sample;
static SWORD *sidari_buffer = NULL;
static SWORD *sidari_buffer2 = NULL;
static unsigned int sidari_buffer_length;

#ifdef SYNCHRONIZED_SOUND
static double sid_ticks_per_tick;
static double sid_ticks;
#endif

static const int autochoose_order_sidari_version[] = { 0, 1, 2,
                                                 -1 };
static const int autochoose_order_sidari_slot[] = { 3, 4, 5, 6, 7, 8, 9, 10,
                                                 -1 };
static const int cfg_vals[] = {
	/* sidari version */
	SIDARI_NO,
	SIDARI_MONO,
	SIDARI_STEREO,
	/* sidari slot */
	SIDARI_SLOT_0,
	SIDARI_SLOT_1,
	SIDARI_SLOT_2,
	SIDARI_SLOT_3,
	SIDARI_SLOT_4,
	SIDARI_SLOT_5,
	SIDARI_SLOT_6,
	SIDARI_SLOT_7
};
static const char * cfg_strings[] = {
	/* sidari version */
	"NO",
	"MONO",
	"STEREO",
	/* sidari slot */
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

int SIDARI_Initialise(int *argc, char *argv[])
{
	int i, j;
	int help_only = FALSE;
	/*Log_print("SIDari_Initialise");*/
	for (i = j = 1; i < *argc; i++) {
		int i_a = (i + 1 < *argc); /* is argument available? */
		int a_m = FALSE; /* error, argument missing! */
		int a_i = FALSE; /* error, argument invalid! */

		if (strcmp(argv[i], "-sidari") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_sidari_version, &SIDARI_version))
					a_i = TRUE;
			}
			else SIDARI_version = SIDARI_MONO;
		}
		else if (strcmp(argv[i], "-sidari-slot") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_sidari_slot, &SIDARI_slot))
					a_i = TRUE;
			}
			else SIDARI_slot = SIDARI_SLOT_3;
		}
		else {
		 	if (strcmp(argv[i], "-help") == 0) {
		 		help_only = TRUE;
				Log_print("\t-sidari [no|mono|stereo]");
				Log_print("\t                 Emulate the SIDari sound card");
				Log_print("\t-sidari-slot [default|0|1|2|3|4|5|6|7]");
				Log_print("\t                 SIDari slot");
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

	if (SIDARI_version != SIDARI_NO) {
		Log_print("SIDari %s enabled in slot %s",
				MatchValue(autochoose_order_sidari_version, &SIDARI_version), 
				MatchValue(autochoose_order_sidari_slot, &SIDARI_slot));
	}

	return TRUE;
}

static void sidari_initialize(unsigned long freq17, int playback_freq, int n_pokeys, int b16, RESID_State *state, RESID_State *state2)
{
	RESID_close(RESID_CHIP_SIDARI_LEFT_INDEX);
	RESID_close(RESID_CHIP_SIDARI_RIGHT_INDEX);
	free(sidari_buffer);
	sidari_buffer = NULL;
	free(sidari_buffer2);
	sidari_buffer2 = NULL;
	if (SIDARI_version != SIDARI_NO) {
		double samples_per_frame;
		unsigned int ticks_per_frame;
		unsigned int surplus_ticks;
		unsigned int max_ticks_per_frame;
		main_freq = freq17;
		dsprate = playback_freq;
		num_pokeys = n_pokeys;
		bit16 = b16;
		SIDARI_clock_freq = 17734472.0 / 18;
		surplus_ticks = ceil(SIDARI_clock_freq / playback_freq);

		/* calculation base is base system clock (!) tick because it's used to clock synchronized sound (taken from pokeysnd.c) */
		samples_per_frame = (double)dsprate / (Atari800_tv_mode == Atari800_TV_PAL ? Atari800_FPS_PAL : Atari800_FPS_NTSC);
		ticks_per_frame = Atari800_tv_mode*ANTIC_LINE_C;
		/* A single call to Atari800_Frame may emulate a bit more CPU ticks than the exact number of
		   ticks per frame (Atari800_tv_mode*114). So we add a few ticks to buffer size just to be safe. */
		max_ticks_per_frame = ticks_per_frame + surplus_ticks;
		ticks_per_sample = (double)ticks_per_frame / samples_per_frame;
		sid_ticks_per_sample = SIDARI_clock_freq / (double)dsprate;
		sidari_buffer_length = (unsigned int)ceil((double)max_ticks_per_frame / ticks_per_sample);
#ifdef SYNCHRONIZED_SOUND
		sid_ticks_per_tick = SIDARI_clock_freq / main_freq;
		sid_ticks = 0.0;
#endif /* SYNCHRONIZED_SOUND */

		RESID_open(RESID_CHIP_SIDARI_LEFT_INDEX);
		if (state != NULL)
			RESID_write_state(RESID_CHIP_SIDARI_LEFT_INDEX, state);
		RESID_init(RESID_CHIP_SIDARI_LEFT_INDEX, SIDARI_clock_freq, sid_model, playback_freq);
		sidari_buffer = Util_malloc(sidari_buffer_length * sizeof(SWORD));
		if (SIDARI_version == SIDARI_STEREO) {
			RESID_open(RESID_CHIP_SIDARI_RIGHT_INDEX);
			if (state2 != NULL)
				RESID_write_state(RESID_CHIP_SIDARI_RIGHT_INDEX, state2);
			RESID_init(RESID_CHIP_SIDARI_RIGHT_INDEX, SIDARI_clock_freq, sid_model, playback_freq);
			sidari_buffer2 = Util_malloc(sidari_buffer_length * sizeof(SWORD));
		}
	}
}

void SIDARI_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16)
{
	RESID_State state;
	RESID_State state2;
	int restore_state = RESID_is_opened(RESID_CHIP_SIDARI_LEFT_INDEX);
	int restore_state2 = RESID_is_opened(RESID_CHIP_SIDARI_RIGHT_INDEX);

	/*Log_print("SIDari_Init");*/

	if (restore_state)
		RESID_read_state(RESID_CHIP_SIDARI_LEFT_INDEX, &state);
	if (restore_state2)
		RESID_read_state(RESID_CHIP_SIDARI_RIGHT_INDEX, &state2);
	sidari_initialize(freq17, playback_freq, n_pokeys, b16, restore_state ? &state : NULL, restore_state2 ? &state2 : NULL);
}

void SIDARI_Exit(void)
{
	/*Log_print("SIDari_Exit");*/

	RESID_close(RESID_CHIP_SIDARI_LEFT_INDEX);
	RESID_close(RESID_CHIP_SIDARI_RIGHT_INDEX);
	free(sidari_buffer);
	sidari_buffer = NULL;
	free(sidari_buffer2);
	sidari_buffer2 = NULL;
}

void SIDARI_Reset(void)
{
	/*Log_print("SIDari_Reset");*/

	sidari_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

void SIDARI_Reinit(int playback_freq)
{
	/*Log_print("SIDari_Reinit");*/

	if (SIDARI_version != SIDARI_NO) {
		dsprate = playback_freq;
		RESID_init(RESID_CHIP_SIDARI_LEFT_INDEX, SIDARI_clock_freq, sid_model, playback_freq);
		if (SIDARI_version == SIDARI_STEREO)
			RESID_init(RESID_CHIP_SIDARI_RIGHT_INDEX, SIDARI_clock_freq, sid_model, playback_freq);
	}
}

int SIDARI_ReadConfig(char *string, char *ptr)
{
	/*Log_print("SIDari_ReadConfig");*/

	if (strcmp(string, "SIDARI_VERSION") == 0) {
		if (!MatchParameter(ptr, autochoose_order_sidari_version, &SIDARI_version))
			return FALSE;
	}
	else if (strcmp(string, "SIDARI_SLOT") == 0) {
		if (!MatchParameter(ptr, autochoose_order_sidari_slot, &SIDARI_slot))
			return FALSE;
	}
	else return FALSE; /* no match */
	return TRUE; /* matched something */
}

void SIDARI_WriteConfig(FILE *fp)
{
	/*Log_print("SIDari_WriteConfig");*/

	fprintf(fp, "SIDARI_VERSION=%s\n", MatchValue(autochoose_order_sidari_version, &SIDARI_version));
	fprintf(fp, "SIDARI_SLOT=%s\n", MatchValue(autochoose_order_sidari_slot, &SIDARI_slot));
}

int SIDARI_InSlot(UWORD addr) {
	int base_address = 0xd500 + 0x20 * SIDARI_slot;
	return (SIDARI_version != SIDARI_NO)
		&& (addr >= base_address)
		&& (addr <= (base_address + (SIDARI_version == SIDARI_MONO ? 0x1f : 0x3f)));
}

int SIDARI_D5GetByte(UWORD addr, int no_side_effects)
{
	int base_address = 0xd500 + 0x20 * SIDARI_slot;
	int result = 0xff;
	if (SIDARI_version == SIDARI_MONO) {
		if ((addr >= base_address) && (addr <= (base_address + 0x1f))) {
			result = 0x33; /* existence indicator */
		}
	}
	else if (SIDARI_version == SIDARI_STEREO) {
		if ((addr >= base_address) && (addr <= (base_address + 0x3f))) {
			result = 0x33; /* existence indicator */
		}
	}
	return result;
}

void SIDARI_D5PutByte(UWORD addr, UBYTE byte)
{
	int base_address = 0xd500 + 0x20 * SIDARI_slot;
	if (SIDARI_version == SIDARI_MONO) {
		if ((addr >= base_address) && (addr <= (base_address + 0x18))) {
			/* SID registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateSIDari();
#endif
			RESID_write(RESID_CHIP_SIDARI_LEFT_INDEX, (UBYTE)(addr - base_address), byte);
		}
	}
	else if (SIDARI_version == SIDARI_STEREO) {
		if ((addr >= base_address) && (addr <= (base_address + 0x18))) {
			/* left SID registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateSIDari();
#endif
			RESID_write(RESID_CHIP_SIDARI_LEFT_INDEX, (UBYTE)(addr - base_address), byte);
		}
		else if ((addr >= (base_address + 0x20)) && (addr <= (base_address + 0x38))) {
			/* right SID registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateSIDari();
#endif
			RESID_write(RESID_CHIP_SIDARI_RIGHT_INDEX, (UBYTE)(addr - (base_address + 0x20)), byte);
		}
	}
}

static UBYTE* generate_samples(UBYTE *sndbuffer, int samples)
{
	int ticks;
	int count;
	unsigned int buflen = samples > sidari_buffer_length ? sidari_buffer_length : samples;
	unsigned int amount = 0;

	if (SIDARI_version != SIDARI_STEREO)
		while (buflen > 0) {
			ticks = buflen * sid_ticks_per_sample;
			count = RESID_calculate_sample(RESID_CHIP_SIDARI_LEFT_INDEX, ticks, sidari_buffer + amount, buflen);
			if (SIDARI_version == SIDARI_STEREO)
				RESID_calculate_sample(RESID_CHIP_SIDARI_RIGHT_INDEX, ticks, sidari_buffer2 + amount, buflen);
			amount += count;
			buflen -= count;
		}
	if (amount > 0) {
		Util_mix(sndbuffer, sidari_buffer, amount, 128, bit16, num_pokeys, 0, 1, 0);
		if (num_pokeys == 2) {
			if (SIDARI_version == SIDARI_STEREO)
				Util_mix(sndbuffer, sidari_buffer2, amount, 128, bit16, num_pokeys, 1, 1, 0);
			else
				Util_mix(sndbuffer, sidari_buffer, amount, 128, bit16, num_pokeys, 1, 1, 0);
		}
		else /*if (num_pokeys == 1)*/ {
			if (SIDARI_version == SIDARI_STEREO)
				Util_mix(sndbuffer, sidari_buffer2, amount, 128, bit16, num_pokeys, 0, 1, 0);
		}
	}
	return sndbuffer + amount * (bit16 ? 2 : 1) * (num_pokeys == 2 ? 2: 1);
}

void SIDARI_Process(void *sndbuffer, int sndn)
{
	/*Log_print("SIDari_Process");*/

	if (SIDARI_version != SIDARI_NO) {
		int two_pokeys = (num_pokeys == 2);
		unsigned int sample_size = (two_pokeys ? 2: 1);
		unsigned int samples_count = sndn / sample_size;
		generate_samples((UBYTE*)sndbuffer, samples_count);
	}
}

#ifdef SYNCHRONIZED_SOUND
#ifndef COUNT_CYCLES
unsigned int SIDARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;

	/*Log_print("SIDari_GenerateSync");*/

	if (SIDARI_version != SIDARI_NO) {
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
unsigned int SIDARI_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;
	if (SIDARI_version != SIDARI_NO) {
		double int_part;
		int ticks;
		unsigned int count = 0;
		unsigned int overclock = 0;
		unsigned int pokeys_count = num_pokeys == 2 ? 2 : 1;
		unsigned int sample_size = (bit16 ? 2 : 1) * pokeys_count;
		unsigned int max_samples_count = (buffer_end - buffer_begin) / sample_size;
		unsigned int requested_samples_count = sndn / sample_size;
		unsigned int samples_count = requested_samples_count > max_samples_count ? max_samples_count : requested_samples_count;
		sid_ticks += num_ticks * sid_ticks_per_tick;
		sid_ticks = modf(sid_ticks, &int_part);
		ticks = int_part;
		/*Log_print("SIDari_GenerateSync");*/
		/*Log_print("sid_ticks=%d, num_ticks=%d", ticks, num_ticks);*/
		if (ticks > 0) {
			count = RESID_calculate_sample(RESID_CHIP_SIDARI_LEFT_INDEX, ticks, sidari_buffer, samples_count);
			if (SIDARI_version == SIDARI_STEREO)
				RESID_calculate_sample(RESID_CHIP_SIDARI_RIGHT_INDEX, ticks, sidari_buffer2, samples_count);
		}
		/* overgenerate ticks to make lacking sample */
		while (count < samples_count) {
			sid_ticks += sid_ticks_per_tick;
			sid_ticks = modf(sid_ticks, &int_part);
			ticks = int_part;
			if (ticks > 0) {
				unsigned int amount = RESID_calculate_sample(RESID_CHIP_SIDARI_LEFT_INDEX, ticks, sidari_buffer + count, 1);
				if (SIDARI_version == SIDARI_STEREO)
					RESID_calculate_sample(RESID_CHIP_SIDARI_RIGHT_INDEX, ticks, sidari_buffer2 + count, 1);
				count += amount;
			}
			overclock++;
		}
		/*unsigned int expected_ticks = samples_count * ticks_per_sample;
		Log_print("over=%d, num=%d, exp=%d, diff=%d", overclock, num_ticks, expected_ticks, num_ticks+overclock-expected_ticks);*/
		sid_ticks -= overclock * sid_ticks_per_tick;
		if (count > 0) {
			Util_mix(buffer, sidari_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
			if (pokeys_count == 2) {
				if (SIDARI_version == SIDARI_STEREO)
					Util_mix(buffer, sidari_buffer2, count, 128, bit16, pokeys_count, 1, 1, 0);
				else
					Util_mix(buffer, sidari_buffer, count, 128, bit16, pokeys_count, 1, 1, 0);
			}
			else /*if (pokeys_count == 1)*/ {
				if (SIDARI_version == SIDARI_STEREO)
					Util_mix(buffer, sidari_buffer2, count, 128, bit16, pokeys_count, 0, 1, 0);
			}
			buffer += count * sample_size;
		}
	}
	return buffer - buffer_begin;
}
#endif
#endif /* SYNCHRONIZED_SOUND */

void SIDARI_StateSave(void)
{
	/*Log_print("SIDari_StateSave");*/

	StateSav_SaveINT(&SIDARI_version, 1);
	if (SIDARI_version != SIDARI_NO) {
		RESID_State state;

		StateSav_SaveINT(&SIDARI_slot, 1);

		RESID_read_state(RESID_CHIP_SIDARI_LEFT_INDEX, &state);

		StateSav_SaveUBYTE(state.sid_register, 0x20);
		StateSav_SaveUBYTE(&state.bus_value, 1);
		StateSav_SaveINT((int*)&state.bus_value_ttl, 1);
		StateSav_SaveINT((int*)state.accumulator, 3);
		StateSav_SaveINT((int*)state.shift_register, 3);
		StateSav_SaveUWORD(state.rate_counter, 3);
		StateSav_SaveUWORD(state.rate_counter_period, 3);
		StateSav_SaveUWORD(state.exponential_counter, 3);
		StateSav_SaveUWORD(state.exponential_counter_period, 3);
		StateSav_SaveUBYTE(state.envelope_counter, 3);
		StateSav_SaveUBYTE((UBYTE*)state.envelope_state, 3);
		StateSav_SaveUBYTE(state.hold_zero, 3);

		if (SIDARI_version == SIDARI_STEREO) {
			RESID_read_state(RESID_CHIP_SIDARI_RIGHT_INDEX, &state);

			StateSav_SaveUBYTE(state.sid_register, 0x20);
			StateSav_SaveUBYTE(&state.bus_value, 1);
			StateSav_SaveINT((int*)&state.bus_value_ttl, 1);
			StateSav_SaveINT((int*)state.accumulator, 3);
			StateSav_SaveINT((int*)state.shift_register, 3);
			StateSav_SaveUWORD(state.rate_counter, 3);
			StateSav_SaveUWORD(state.rate_counter_period, 3);
			StateSav_SaveUWORD(state.exponential_counter, 3);
			StateSav_SaveUWORD(state.exponential_counter_period, 3);
			StateSav_SaveUBYTE(state.envelope_counter, 3);
			StateSav_SaveUBYTE((UBYTE*)state.envelope_state, 3);
			StateSav_SaveUBYTE(state.hold_zero, 3);
		}
	}
}

void SIDARI_StateRead(void)
{
	/*Log_print("SIDari_StateRead");*/

	StateSav_ReadINT(&SIDARI_version, 1);
	if (SIDARI_version != SIDARI_NO) {
		RESID_State state;

		StateSav_ReadINT(&SIDARI_slot, 1);

		StateSav_ReadUBYTE(state.sid_register, 0x20);
		StateSav_ReadUBYTE(&state.bus_value, 1);
		StateSav_ReadINT((int*)&state.bus_value_ttl, 1);
		StateSav_ReadINT((int*)state.accumulator, 3);
		StateSav_ReadINT((int*)state.shift_register, 3);
		StateSav_ReadUWORD(state.rate_counter, 3);
		StateSav_ReadUWORD(state.rate_counter_period, 3);
		StateSav_ReadUWORD(state.exponential_counter, 3);
		StateSav_ReadUWORD(state.exponential_counter_period, 3);
		StateSav_ReadUBYTE(state.envelope_counter, 3);
		StateSav_ReadUBYTE((UBYTE*)state.envelope_state, 3);
		StateSav_ReadUBYTE(state.hold_zero, 3);

		if (SIDARI_version == SIDARI_STEREO) {
			RESID_State state2;

			StateSav_ReadUBYTE(state2.sid_register, 0x20);
			StateSav_ReadUBYTE(&state2.bus_value, 1);
			StateSav_ReadINT((int*)&state2.bus_value_ttl, 1);
			StateSav_ReadINT((int*)state2.accumulator, 3);
			StateSav_ReadINT((int*)state2.shift_register, 3);
			StateSav_ReadUWORD(state2.rate_counter, 3);
			StateSav_ReadUWORD(state2.rate_counter_period, 3);
			StateSav_ReadUWORD(state2.exponential_counter, 3);
			StateSav_ReadUWORD(state2.exponential_counter_period, 3);
			StateSav_ReadUBYTE(state2.envelope_counter, 3);
			StateSav_ReadUBYTE((UBYTE*)state2.envelope_state, 3);
			StateSav_ReadUBYTE(state2.hold_zero, 3);

			sidari_initialize(main_freq, dsprate, num_pokeys, bit16, &state, &state2);
		}
		else
			sidari_initialize(main_freq, dsprate, num_pokeys, bit16, &state, NULL);
	}
	else
		sidari_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

/*
vim:ts=4:sw=4:
*/

