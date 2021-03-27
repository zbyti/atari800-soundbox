/*
 * slightsid.c - Emulation of the SlightSID sound card.
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

/*TODO: extin */
#define COUNT_CYCLES

#include "slightsid.h"
#include "pokeysnd.h"
#include "atari.h"
#include "antic.h"
#include "util.h"
#include "statesav.h"
#include "log.h"
#include <stdlib.h>
#include <math.h>


int SLIGHTSID_version = SLIGHTSID_NO;

/* Original Commodore 64 SID chip is clocked by 985248 Hz in PAL and 1022730 Hz in NTSC */
double SLIGHTSID_clock_freq;

static int sid_model = RESID_SID_MODEL_8580;
static unsigned long main_freq;
static int bit16;
static int num_pokeys;
static int dsprate;
static double ticks_per_sample;
static double sid_ticks_per_sample;
static SWORD *slightsid_buffer = NULL;
static SWORD *slightsid_buffer2 = NULL;
static unsigned int slightsid_buffer_length;

#ifdef SYNCHRONIZED_SOUND
static double sid_ticks_per_tick;
static double sid_ticks;
#endif

/* SlightSID configuration register.
 * b0: clock: 0-PAL, 1-NTSC
 * b1: addressing: 0-independent, 1-parallel
 * b2: reset: 0-reset, 1-normal operation
 * b7: parity bit
 */
static UBYTE config = 0x84;
static int ntsc = FALSE;
static int parallel = FALSE;
static int reset = FALSE;

static const int autochoose_order_slightsid_version[] = { 0, 1, 2,
                                                 -1 };
static const int cfg_vals[] = {
	/* slightsid version */
	SLIGHTSID_NO,
	SLIGHTSID_MONO,
	SLIGHTSID_STEREO
};
static const char * cfg_strings[] = {
	/* slightsid version */
	"NO",
	"MONO",
	"STEREO"
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

int SLIGHTSID_Initialise(int *argc, char *argv[])
{
	int i, j;
	int help_only = FALSE;
	/*Log_print("SlightSID_Initialise");*/
	for (i = j = 1; i < *argc; i++) {
		int i_a = (i + 1 < *argc); /* is argument available? */
		int a_m = FALSE; /* error, argument missing! */
		int a_i = FALSE; /* error, argument invalid! */

		if (strcmp(argv[i], "-slightsid") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_slightsid_version, &SLIGHTSID_version))
					a_i = TRUE;
			}
			else SLIGHTSID_version = SLIGHTSID_MONO;
		}
		else {
		 	if (strcmp(argv[i], "-help") == 0) {
		 		help_only = TRUE;
				Log_print("\t-slightsid [no|mono|stereo]");
				Log_print("\t                 Emulate the SlightSID sound card");
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

	if (SLIGHTSID_version != SLIGHTSID_NO) {
		Log_print("SlightSID %s enabled", MatchValue(autochoose_order_slightsid_version, &SLIGHTSID_version));
	}

	return TRUE;
}

static void slightsid_initialize(unsigned long freq17, int playback_freq, int n_pokeys, int b16, RESID_State *state, RESID_State *state2)
{
	RESID_close(RESID_CHIP_SLIGHTSID_LEFT_INDEX);
	RESID_close(RESID_CHIP_SLIGHTSID_RIGHT_INDEX);
	free(slightsid_buffer);
	slightsid_buffer = NULL;
	free(slightsid_buffer2);
	slightsid_buffer2 = NULL;
	if (SLIGHTSID_version != SLIGHTSID_NO) {
		double samples_per_frame;
		unsigned int ticks_per_frame;
		unsigned int surplus_ticks;
		unsigned int max_ticks_per_frame;
		main_freq = freq17;
		dsprate = playback_freq;
		num_pokeys = n_pokeys;
		bit16 = b16;
		SLIGHTSID_clock_freq = ((SLIGHTSID_version == SLIGHTSID_STEREO) && ntsc) ? (14318182.0 / 14) : (17734475.0 / 18);
		surplus_ticks = ceil(SLIGHTSID_clock_freq / playback_freq);

		/* calculation base is base system clock (!) tick because it's used to clock synchronized sound (taken from pokeysnd.c) */
		samples_per_frame = (double)dsprate / (Atari800_tv_mode == Atari800_TV_PAL ? Atari800_FPS_PAL : Atari800_FPS_NTSC);
		ticks_per_frame = Atari800_tv_mode*ANTIC_LINE_C;
		/* A single call to Atari800_Frame may emulate a bit more CPU ticks than the exact number of
		   ticks per frame (Atari800_tv_mode*114). So we add a few ticks to buffer size just to be safe. */
		max_ticks_per_frame = ticks_per_frame + surplus_ticks;
		ticks_per_sample = (double)ticks_per_frame / samples_per_frame;
		sid_ticks_per_sample = SLIGHTSID_clock_freq / (double)dsprate;
		slightsid_buffer_length = (unsigned int)ceil((double)max_ticks_per_frame / ticks_per_sample);
#ifdef SYNCHRONIZED_SOUND
		sid_ticks_per_tick = SLIGHTSID_clock_freq / main_freq;
		sid_ticks = 0.0;
#endif /* SYNCHRONIZED_SOUND */

		RESID_open(RESID_CHIP_SLIGHTSID_LEFT_INDEX);
		if (state != NULL)
			RESID_write_state(RESID_CHIP_SLIGHTSID_LEFT_INDEX, state);
		RESID_init(RESID_CHIP_SLIGHTSID_LEFT_INDEX, SLIGHTSID_clock_freq, sid_model, playback_freq);
		slightsid_buffer = Util_malloc(slightsid_buffer_length * sizeof(SWORD));
		if (SLIGHTSID_version == SLIGHTSID_STEREO) {
			RESID_open(RESID_CHIP_SLIGHTSID_RIGHT_INDEX);
			if (state2 != NULL)
				RESID_write_state(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, state2);
			RESID_init(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, SLIGHTSID_clock_freq, sid_model, playback_freq);
			slightsid_buffer2 = Util_malloc(slightsid_buffer_length * sizeof(SWORD));
		}
	}
}

void SLIGHTSID_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16)
{
	RESID_State state;
	RESID_State state2;
	int restore_state = RESID_is_opened(RESID_CHIP_SLIGHTSID_LEFT_INDEX);
	int restore_state2 = RESID_is_opened(RESID_CHIP_SLIGHTSID_RIGHT_INDEX);

	/*Log_print("SlightSID_Init");*/

	if (restore_state)
		RESID_read_state(RESID_CHIP_SLIGHTSID_LEFT_INDEX, &state);
	if (restore_state2)
		RESID_read_state(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, &state2);
	slightsid_initialize(freq17, playback_freq, n_pokeys, b16, restore_state ? &state : NULL, restore_state2 ? &state2 : NULL);
}

void SLIGHTSID_Exit(void)
{
	/*Log_print("SlightSID_Exit");*/

	RESID_close(RESID_CHIP_SLIGHTSID_LEFT_INDEX);
	RESID_close(RESID_CHIP_SLIGHTSID_RIGHT_INDEX);
	free(slightsid_buffer);
	slightsid_buffer = NULL;
	free(slightsid_buffer2);
	slightsid_buffer2 = NULL;
}

static void update_config(UBYTE byte)
{
	config = byte;

	ntsc = (byte & 0x01) != 0;
	parallel = (byte & 0x02) != 0;
	reset = (byte & 0x04) == 0;
}

void SLIGHTSID_Reset(void)
{
	/*Log_print("SlightSID_Reset");*/

	if (SLIGHTSID_version == SLIGHTSID_STEREO)
		update_config(0x84);
	slightsid_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

void SLIGHTSID_Reinit(int playback_freq)
{
	/*Log_print("SlightSID_Reinit");*/

	if (SLIGHTSID_version != SLIGHTSID_NO) {
		dsprate = playback_freq;
		RESID_init(RESID_CHIP_SLIGHTSID_LEFT_INDEX, SLIGHTSID_clock_freq, sid_model, playback_freq);
		if (SLIGHTSID_version == SLIGHTSID_STEREO)
			RESID_init(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, SLIGHTSID_clock_freq, sid_model, playback_freq);
	}
}

int SLIGHTSID_ReadConfig(char *string, char *ptr)
{
	/*Log_print("SlightSID_ReadConfig");*/

	if (strcmp(string, "SLIGHTSID_VERSION") == 0) {
		if (!MatchParameter(ptr, autochoose_order_slightsid_version, &SLIGHTSID_version))
			return FALSE;
	}
	else return FALSE; /* no match */
	return TRUE; /* matched something */
}

void SLIGHTSID_WriteConfig(FILE *fp)
{
	/*Log_print("SlightSID_WriteConfig");*/

	fprintf(fp, "SLIGHTSID_VERSION=%s\n", MatchValue(autochoose_order_slightsid_version, &SLIGHTSID_version));
}

int SLIGHTSID_D5GetByte(UWORD addr, int no_side_effects)
{
	int result = 0xff;
	if (SLIGHTSID_version == SLIGHTSID_MONO) {
		if (addr <= 0xd57f) {
			result = 0x33; /* existence indicator */
		}
	}
	else if (SLIGHTSID_version == SLIGHTSID_STEREO) {
		if (addr <= 0xd53f) {
			result = 0x33; /* existence indicator */
		}
		else if (addr == 0xd540) {
			/* data read register */
		}
		else if (addr == 0xd541) {
			result = config; /* configuration register */
		}
	}
	return result;
}

static int is_parity_even(UBYTE byte) 
{
	int i;
	int p = 1;
	for (i = 0; i < 8; i++)
		p ^= (byte >> i) & 1;
	return p != 0;
}

void SLIGHTSID_D5PutByte(UWORD addr, UBYTE byte)
{
	if (SLIGHTSID_version == SLIGHTSID_MONO) {
		int offset = addr & 0xff9f;
		if (offset <= 0xd518) {
			/* SID registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateSlightSID();
#endif
			RESID_write(RESID_CHIP_SLIGHTSID_LEFT_INDEX, (UBYTE)(offset - 0xd500), byte);
		}
	}
	else if (SLIGHTSID_version == SLIGHTSID_STEREO) {
		if ((addr <= 0xd518) && (!reset)) {
			/* left SID registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateSlightSID();
#endif
			RESID_write(RESID_CHIP_SLIGHTSID_LEFT_INDEX, (UBYTE)(addr - 0xd500), byte);
			if (parallel)
				RESID_write(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, (UBYTE)(addr - 0xd500), byte);
		}
		else if ((addr >= 0xd520) && (addr <= 0xd538) && (!reset)) {
			/* right SID registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateSlightSID();
#endif
			if (parallel)
				RESID_write(RESID_CHIP_SLIGHTSID_LEFT_INDEX, (UBYTE)(addr - 0xd520), byte);
			RESID_write(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, (UBYTE)(addr - 0xd520), byte);
		}
		else if (addr == 0xd540) {
			/* data register */
		}
		else if (addr == 0xd541) {
			/* configuration register */
			if (is_parity_even(byte)) {
				RESID_State state;
				RESID_State state2;
#ifdef SYNCHRONIZED_SOUND
				POKEYSND_UpdateSlightSID();
#endif
				RESID_read_state(RESID_CHIP_SLIGHTSID_LEFT_INDEX, &state);
				RESID_read_state(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, &state2);
				update_config(byte);
				slightsid_initialize(main_freq, dsprate, num_pokeys, bit16, &state, &state2);
			}
		}
	}
}

static UBYTE* generate_samples(UBYTE *sndbuffer, int samples)
{
	int ticks;
	int count;
	unsigned int buflen = samples > slightsid_buffer_length ? slightsid_buffer_length : samples;
	unsigned int amount = 0;

	if ((SLIGHTSID_version != SLIGHTSID_STEREO) || (!reset))
		while (buflen > 0) {
			ticks = buflen * sid_ticks_per_sample;
			count = RESID_calculate_sample(RESID_CHIP_SLIGHTSID_LEFT_INDEX, ticks, slightsid_buffer + amount, buflen);
			if (SLIGHTSID_version == SLIGHTSID_STEREO)
				RESID_calculate_sample(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, ticks, slightsid_buffer2 + amount, buflen);
			amount += count;
			buflen -= count;
		}
	if (amount > 0) {
		Util_mix(sndbuffer, slightsid_buffer, amount, 128, bit16, num_pokeys, 0, 1, 0);
		if (num_pokeys == 2) {
			if (SLIGHTSID_version == SLIGHTSID_STEREO)
				Util_mix(sndbuffer, slightsid_buffer2, amount, 128, bit16, num_pokeys, 1, 1, 0);
			else
				Util_mix(sndbuffer, slightsid_buffer, amount, 128, bit16, num_pokeys, 1, 1, 0);
		}
		else /*if (num_pokeys == 1)*/ {
			if (SLIGHTSID_version == SLIGHTSID_STEREO)
				Util_mix(sndbuffer, slightsid_buffer2, amount, 128, bit16, num_pokeys, 0, 1, 0);
		}
	}
	return sndbuffer + amount * (bit16 ? 2 : 1) * (num_pokeys == 2 ? 2: 1);
}

void SLIGHTSID_Process(void *sndbuffer, int sndn)
{
	/*Log_print("SlightSID_Process");*/

	if (SLIGHTSID_version != SLIGHTSID_NO) {
		int two_pokeys = (num_pokeys == 2);
		unsigned int sample_size = (two_pokeys ? 2: 1);
		unsigned int samples_count = sndn / sample_size;
		generate_samples((UBYTE*)sndbuffer, samples_count);
	}
}

#ifdef SYNCHRONIZED_SOUND
#ifndef COUNT_CYCLES
unsigned int SLIGHTSID_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;

	/*Log_print("SlightSID_GenerateSync");*/

	if (SLIGHTSID_version != SLIGHTSID_NO) {
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
unsigned int SLIGHTSID_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;
	if ((SLIGHTSID_version == SLIGHTSID_MONO) || ((SLIGHTSID_version == SLIGHTSID_STEREO) && (!reset))) {
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
		/*Log_print("SlightSID_GenerateSync");*/
		/*Log_print("sid_ticks=%d, num_ticks=%d", ticks, num_ticks);*/
		if (ticks > 0) {
			count = RESID_calculate_sample(RESID_CHIP_SLIGHTSID_LEFT_INDEX, ticks, slightsid_buffer, samples_count);
			if (SLIGHTSID_version == SLIGHTSID_STEREO)
				RESID_calculate_sample(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, ticks, slightsid_buffer2, samples_count);
		}
		/* overgenerate ticks to make lacking sample */
		while (count < samples_count) {
			sid_ticks += sid_ticks_per_tick;
			sid_ticks = modf(sid_ticks, &int_part);
			ticks = int_part;
			if (ticks > 0) {
				unsigned int amount = RESID_calculate_sample(RESID_CHIP_SLIGHTSID_LEFT_INDEX, ticks, slightsid_buffer + count, 1);
				if (SLIGHTSID_version == SLIGHTSID_STEREO)
					RESID_calculate_sample(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, ticks, slightsid_buffer2 + count, 1);
				count += amount;
			}
			overclock++;
		}
		/*unsigned int expected_ticks = samples_count * ticks_per_sample;
		Log_print("over=%d, num=%d, exp=%d, diff=%d", overclock, num_ticks, expected_ticks, num_ticks+overclock-expected_ticks);*/
		sid_ticks -= overclock * sid_ticks_per_tick;
		if (count > 0) {
			Util_mix(buffer, slightsid_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
			if (pokeys_count == 2) {
				if (SLIGHTSID_version == SLIGHTSID_STEREO)
					Util_mix(buffer, slightsid_buffer2, count, 128, bit16, pokeys_count, 1, 1, 0);
				else
					Util_mix(buffer, slightsid_buffer, count, 128, bit16, pokeys_count, 1, 1, 0);
			}
			else /*if (pokeys_count == 1)*/ {
				if (SLIGHTSID_version == SLIGHTSID_STEREO)
					Util_mix(buffer, slightsid_buffer2, count, 128, bit16, pokeys_count, 0, 1, 0);
			}
			buffer += count * sample_size;
		}
	}
	return buffer - buffer_begin;
}
#endif
#endif /* SYNCHRONIZED_SOUND */

void SLIGHTSID_StateSave(void)
{
	/*Log_print("SlightSID_StateSave");*/

	StateSav_SaveINT(&SLIGHTSID_version, 1);
	if (SLIGHTSID_version != SLIGHTSID_NO) {
		RESID_State state;

		RESID_read_state(RESID_CHIP_SLIGHTSID_LEFT_INDEX, &state);

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

		if (SLIGHTSID_version == SLIGHTSID_STEREO) {
			StateSav_SaveUBYTE(&config, 1);

			RESID_read_state(RESID_CHIP_SLIGHTSID_RIGHT_INDEX, &state);

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

void SLIGHTSID_StateRead(void)
{
	/*Log_print("SlightSID_StateRead");*/

	StateSav_ReadINT(&SLIGHTSID_version, 1);
	if (SLIGHTSID_version != SLIGHTSID_NO) {
		RESID_State state;

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

		if (SLIGHTSID_version == SLIGHTSID_STEREO) {
			RESID_State state2;

			StateSav_ReadUBYTE(&config, 1);

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

			update_config(config);
			slightsid_initialize(main_freq, dsprate, num_pokeys, bit16, &state, &state2);
		}
		else
			slightsid_initialize(main_freq, dsprate, num_pokeys, bit16, &state, NULL);
	}
	else
		slightsid_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

/*
vim:ts=4:sw=4:
*/

