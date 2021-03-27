/*
 * evie.c - Emulation of the Evie sound card.
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
 * 1. SID linear filter
 * 2. COVOX
 * 3. LEDs
 * 4. exclude SID or PSG depending on --enable-sid_emulation (SID_EMU) or --enable-psg_emulation (PSG_EMU) configuration parameters
 */

#define COUNT_CYCLES

#include "evie.h"
#include "pokeysnd.h"
#include "atari.h"
#include "antic.h"
#include "util.h"
#include "statesav.h"
#include "log.h"
#include <stdlib.h>
#include <math.h>


int EVIE_version = EVIE_2_0;
int EVIE_covox_page = EVIE_COVOX_D7;
/* Original Commodore 64 SID chip is clocked by 985248 Hz in PAL and 1022730 Hz in NTSC */
double EVIE_sid_clock_freq;
double EVIE_psg_clock_freq;

static unsigned long main_freq;
static int bit16;
static int num_pokeys;
static int dsprate;
static double ticks_per_sample;

static const int sid_model[] = { RESID_SID_FILTER_NONE, RESID_SID_FILTER_LINEAR, RESID_SID_MODEL_6581, RESID_SID_MODEL_8580 };
static double sid_ticks_per_sample;
static SWORD *sid_buffer = NULL;
static unsigned int sid_buffer_length;

static const int psg_model = AYEMU_PSG_MODEL_YM;
static int psg_pan;
static double psg_ticks_per_sample;
static SWORD *psg_buffer = NULL;
static unsigned int psg_buffer_length;

#ifdef SYNCHRONIZED_SOUND
static double sid_ticks_per_tick;
static double sid_ticks;

static double psg_ticks_per_tick;
static double psg_ticks;
#endif

/* Evie configuration register.
 * b10: SID filter: %00=no, %01-linear, %10-6581, %11-8580
 * b2: PSG /SEL: 0=master clock, 1=master clock / 2
 * b3: PSG master clock: 0=system clock, 1=2MHz
 * b4: ScrollLock LED: 1=on
 * b5: NumLock LED: 1=on
 * b6: CapsLock LED: 1=on
 * b7: SID on $D5xx: 1=yes
 */
static UBYTE config = 0x00;
static int sid_filter = 0;
static int psg_div2 = FALSE;
static int psg_2mhz = FALSE;
static int scrolllock_led = FALSE;
static int numlock_led = FALSE;
static int capslock_led = FALSE;
static int sid_d5 = FALSE;
static UBYTE psg_register = 0x00;


static const int autochoose_order_evie_version[] = { 0, 1, 2,
                                                 -1 };
static const int autochoose_order_evie_covox_page[] = { 3, 4,
                                                 -1 };
static const int cfg_vals[] = {
	/* evie version */
	EVIE_NO,
	EVIE_1_0,
	EVIE_2_0,
	/* covox page */
	EVIE_COVOX_D6,
	EVIE_COVOX_D7,
};
static const char * cfg_strings[] = {
	/* evie version */
	"NO",
	"1.0",
	"2.0",
	/* covox page */
	"D6",
	"D7",
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

int EVIE_Initialise(int *argc, char *argv[])
{
	int i, j;
	int help_only = FALSE;
	/*Log_print("Evie_Initialise");*/
	for (i = j = 1; i < *argc; i++) {
		int i_a = (i + 1 < *argc); /* is argument available? */
		int a_m = FALSE; /* error, argument missing! */
		int a_i = FALSE; /* error, argument invalid! */

		if (strcmp(argv[i], "-evie") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_evie_version, &EVIE_version))
					a_i = TRUE;
			}
			else EVIE_version = EVIE_2_0;
		}
		else if (strcmp(argv[i], "-evie-covox") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_evie_covox_page, &EVIE_covox_page))
					a_i = TRUE;
			}
			else EVIE_covox_page = EVIE_COVOX_D7;
		}
		else {
		 	if (strcmp(argv[i], "-help") == 0) {
		 		help_only = TRUE;
				Log_print("\t-evie [no|1.0|2.0]");
				Log_print("\t                 Emulate the Evie sound card");
				Log_print("\t-evie-covox [d6|d7]");
				Log_print("\t                 Select COVOX page of Evie sound card");
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

	if (EVIE_version != EVIE_NO) {
		Log_print("Evie %s enabled", MatchValue(autochoose_order_evie_version, &EVIE_version));
	}

	return TRUE;
}

static void evie_initialize(unsigned long freq17, int playback_freq, int n_pokeys, int b16, RESID_State *sid_state, AYEMU_State *psg_state)
{
	RESID_close(RESID_CHIP_EVIE_INDEX);
	free(sid_buffer);
	sid_buffer = NULL;
	AYEMU_close(AYEMU_CHIP_EVIE_INDEX);
	free(psg_buffer);
	psg_buffer = NULL;
	if (EVIE_version != EVIE_NO) {
		double samples_per_frame;
		unsigned int ticks_per_frame;
		unsigned int sid_surplus_ticks;
		unsigned int sid_max_ticks_per_frame;
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

		if (EVIE_version == EVIE_1_0) {
			EVIE_sid_clock_freq = 24576000.0 * 5 / 128;
			EVIE_psg_clock_freq = psg_2mhz ? 24576000.0 * 5 / 64 : base_clock;
		}
		else /*if (EVIE_version == EVIE_2_0)*/ {
			EVIE_sid_clock_freq = base_clock * 10 / 18;
			EVIE_psg_clock_freq = psg_2mhz ? base_clock * 22 / (1.5 * 13) : base_clock;
		}
		if (psg_div2) {
			EVIE_psg_clock_freq /= 2;
		}

		sid_surplus_ticks = ceil(EVIE_sid_clock_freq / playback_freq);
		sid_max_ticks_per_frame = ticks_per_frame + sid_surplus_ticks;
		sid_ticks_per_sample = EVIE_sid_clock_freq / (double)dsprate;
		sid_buffer_length = (unsigned int)ceil((double)sid_max_ticks_per_frame / ticks_per_sample);

		/*Log_print("evie_initialize psg_clk: %f", EVIE_psg_clock_freq);*/
		psg_pan = ((num_pokeys == 2) && (EVIE_version == EVIE_2_0)) ? AYEMU_PSG_PAN_ABC : AYEMU_PSG_PAN_MONO;
		psg_surplus_ticks = ceil(EVIE_psg_clock_freq / playback_freq);
		psg_max_ticks_per_frame = ticks_per_frame + psg_surplus_ticks;
		psg_ticks_per_sample = EVIE_psg_clock_freq / (double)dsprate;
		psg_buffer_length = (unsigned int)ceil((double)psg_max_ticks_per_frame / ticks_per_sample);
#ifdef SYNCHRONIZED_SOUND
		sid_ticks_per_tick = EVIE_sid_clock_freq / main_freq;
		sid_ticks = 0.0;
		psg_ticks_per_tick = EVIE_psg_clock_freq / main_freq;
		psg_ticks = 0.0;
#endif /* SYNCHRONIZED_SOUND */

		RESID_open(RESID_CHIP_EVIE_INDEX);
		if (sid_state != NULL)
			RESID_write_state(RESID_CHIP_EVIE_INDEX, sid_state);
		RESID_init(RESID_CHIP_EVIE_INDEX, EVIE_sid_clock_freq, sid_model[sid_filter], playback_freq);
		sid_buffer = Util_malloc(sid_buffer_length * sizeof(SWORD));

		AYEMU_open(AYEMU_CHIP_EVIE_INDEX);
		if (psg_state != NULL)
			AYEMU_write_state(AYEMU_CHIP_EVIE_INDEX, psg_state);
		AYEMU_init(AYEMU_CHIP_EVIE_INDEX, EVIE_psg_clock_freq, psg_model, psg_pan, playback_freq);
		psg_buffer = Util_malloc(psg_buffer_length * (num_pokeys == 2 ? 2 : 1) * sizeof(SWORD));
	}
}

void EVIE_Init(unsigned long freq17, int playback_freq, int n_pokeys, int b16)
{
	RESID_State sid_state;
	AYEMU_State psg_state;
	int restore_sid_state = RESID_is_opened(RESID_CHIP_EVIE_INDEX);
	int restore_psg_state = AYEMU_is_opened(AYEMU_CHIP_EVIE_INDEX);

	/*Log_print("Evie_Init");*/

	if (restore_sid_state)
		RESID_read_state(RESID_CHIP_EVIE_INDEX, &sid_state);
	if (restore_psg_state)
		AYEMU_read_state(AYEMU_CHIP_EVIE_INDEX, &psg_state);

	evie_initialize(freq17, playback_freq, n_pokeys, b16, restore_sid_state ? &sid_state : NULL, restore_psg_state ? &psg_state : NULL);
}

void EVIE_Exit(void)
{
	/*Log_print("Evie_Exit");*/

	RESID_close(RESID_CHIP_EVIE_INDEX);
	free(sid_buffer);
	sid_buffer = NULL;

	AYEMU_close(AYEMU_CHIP_EVIE_INDEX);
	free(psg_buffer);
	psg_buffer = NULL;
}

static void update_config(UBYTE byte)
{
	config = byte;

	sid_filter = (byte & 0x03);
	psg_div2 = (byte & 0x04) != 0;
	psg_2mhz = (byte & 0x08) != 0;
	scrolllock_led = (byte & 0x10) != 0;
	numlock_led = (byte & 0x20) != 0;
	capslock_led = (byte & 0x40) != 0;
	sid_d5 = (byte & 0x80) != 0;
}

void EVIE_Reset(void)
{
	/*Log_print("Evie_Reset");*/

	if (EVIE_version != EVIE_NO) {
		update_config(0x00);
		psg_register = 0x00;
	}
	evie_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

void EVIE_Reinit(int playback_freq)
{
	/*Log_print("Evie_Reinit");*/

	if (EVIE_version != EVIE_NO) {
		dsprate = playback_freq;
		RESID_init(RESID_CHIP_EVIE_INDEX, EVIE_sid_clock_freq, sid_model[sid_filter], playback_freq);
		AYEMU_init(AYEMU_CHIP_EVIE_INDEX, EVIE_psg_clock_freq, psg_model, psg_pan, playback_freq);
	}
}

int EVIE_ReadConfig(char *string, char *ptr)
{
	/*Log_print("Evie_ReadConfig");*/

	if (strcmp(string, "EVIE_VERSION") == 0) {
		if (!MatchParameter(ptr, autochoose_order_evie_version, &EVIE_version))
			return FALSE;
	}
	else if (strcmp(string, "EVIE_COVOX") == 0) {
		if (!MatchParameter(ptr, autochoose_order_evie_covox_page, &EVIE_covox_page))
			return FALSE;
	}
	else return FALSE; /* no match */
	return TRUE; /* matched something */
}

void EVIE_WriteConfig(FILE *fp)
{
	/*Log_print("Evie_WriteConfig");*/

	fprintf(fp, "EVIE_VERSION=%s\n", MatchValue(autochoose_order_evie_version, &EVIE_version));
	fprintf(fp, "EVIE_COVOX=%s\n", MatchValue(autochoose_order_evie_covox_page, &EVIE_covox_page));
}

static const char signature[5] = { 'E', 'v', 'i', 'e' };

int EVIE_D2GetByte(UWORD addr, int no_side_effects)
{
	int result = 0xff;
	if ((EVIE_version != EVIE_NO) && (addr >= 0xd280)) {
		int offset = addr & 0x3f;
		if (offset <= 0x1f) {
			/* SID registers */
			if ((offset >= 0x19) && (offset <= 0x1c)) {
				result = RESID_read(RESID_CHIP_EVIE_INDEX, (UBYTE)(offset - 0x00));
			}
			else {
				result = 0x33;
			}
		}
		else if (offset <= 0x2f) {
			/* PSG registers */
			result = AYEMU_read(AYEMU_CHIP_EVIE_INDEX, (UBYTE)(offset - 0x20));
		}
		else if (offset == 0x30) {
			/* PSG read data / register select */
			result = AYEMU_read(AYEMU_CHIP_EVIE_INDEX, psg_register & 0x0f);
		}
		else if (offset == 0x31) {
			/* PSG write data / read register address */
			result = psg_register;
		}
		else if ((offset >= 0x3a) && (offset <= 0x3e)) {
			if (offset < 0x3e) {
				/* Evie signature */
				result = signature[offset - 0x3a];
			}
			else {
				result = EVIE_version == EVIE_2_0 ? 0x20 : 0x10;
			}
		}
		else if (offset == 0x3f) {
			/* configuration register */
			result = config;
		}
	}
	return result;
}

void EVIE_D2PutByte(UWORD addr, UBYTE byte)
{
	if (EVIE_version != EVIE_NO) {
		if (addr >= 0xd280) {
			int offset = addr & 0x3f;
			if (offset <= 0x1f) {
				/* SID registers */
#ifdef SYNCHRONIZED_SOUND
				POKEYSND_UpdateEvie();
#endif
				RESID_write(RESID_CHIP_EVIE_INDEX, (UBYTE)(offset - 0x00), byte);
			}
			else if (offset <= 0x2f) {
				/* PSG registers */
#ifdef SYNCHRONIZED_SOUND
				POKEYSND_UpdateEvie();
#endif
				AYEMU_write(AYEMU_CHIP_EVIE_INDEX, (UBYTE)(offset - 0x20), byte);
			}
			else if (offset == 0x30) {
				/* PSG read data / register select */
				psg_register = byte;
			}
			else if (offset == 0x31) {
				/* PSG write data / register address */
#ifdef SYNCHRONIZED_SOUND
				POKEYSND_UpdateEvie();
#endif
				AYEMU_write(AYEMU_CHIP_EVIE_INDEX, psg_register & 0x0f, byte);
			}
			else if (offset == 0x3f) {
				/* configuration register */
				RESID_State sid_state;
				AYEMU_State psg_state;
#ifdef SYNCHRONIZED_SOUND
				POKEYSND_UpdateEvie();
#endif
				RESID_read_state(RESID_CHIP_EVIE_INDEX, &sid_state);
				AYEMU_read_state(AYEMU_CHIP_EVIE_INDEX, &psg_state);
				update_config(byte);
				evie_initialize(main_freq, dsprate, num_pokeys, bit16, &sid_state, &psg_state);
			}
		}
	}
}

int EVIE_D5GetByte(UWORD addr, int no_side_effects)
{
#if FALSE
	/* 
	 * mapped registers return 0xff not 0x33 as they should,
	 * so read POTX, POTY, OSC3 and ENV3 is possible only at $D2xx
	 */
	return 0xff;
#else
	int result = 0xff;
	if ((EVIE_version != EVIE_NO) && sid_d5) {
		UWORD offset = addr & 0xffbf;
		if (offset <= 0xd51f) {
			if ((offset >= 0xd519) && (offset <= 0xd51c)) {
				result = RESID_read(RESID_CHIP_EVIE_INDEX, (UBYTE)(offset - 0xd500));
			}
			else {
				result = 0x33; /* SID indicator */
			}
		}
	}
	return result;
#endif
}

void EVIE_D5PutByte(UWORD addr, UBYTE byte)
{
	if ((EVIE_version != EVIE_NO) && sid_d5) {
		UWORD address = addr & 0xffbf;
		if (address <= 0xd51f) {
			/* SID registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateEvie();
#endif
			RESID_write(RESID_CHIP_EVIE_INDEX, (UBYTE)(address - 0xd500), byte);
		}
	}
}

void EVIE_D67PutByte(UWORD addr, UBYTE byte)
{
	if (EVIE_version != EVIE_NO) {
		int base_addr = EVIE_covox_page * 0x100;
		if (addr <= (base_addr + 3)) {
			/* COVOX channel registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateEvie();
#endif
			/*COVOX_write(0, (UBYTE)(addr - base_addr), byte);*/
		}
		else if (addr <= (base_addr + 7)) {
			/* COVOX channel 1+2 parallel write registers */
#ifdef SYNCHRONIZED_SOUND
			POKEYSND_UpdateEvie();
#endif
			/*COVOX_write(0, (UBYTE)0, byte);
			COVOX_write(0, (UBYTE)1, byte);*/
		}
	}
}

static UBYTE* sid_generate_samples(UBYTE *sndbuffer, int samples)
{
	int ticks;
	int count;
	unsigned int pokeys_count = num_pokeys == 2 ? 2 : 1;
	unsigned int buflen = samples > sid_buffer_length ? sid_buffer_length : samples;
	unsigned int amount = 0;

	if (EVIE_version != EVIE_NO)
		while (buflen > 0) {
			ticks = buflen * sid_ticks_per_sample;
			count = RESID_calculate_sample(RESID_CHIP_EVIE_INDEX, ticks, sid_buffer + amount, buflen);
			amount += count;
			buflen -= count;
		}
	if (amount > 0) {
		Util_mix(sndbuffer, sid_buffer, amount, 128, bit16, pokeys_count, 0, 1, 0);
		if (pokeys_count == 2)
			Util_mix(sndbuffer, sid_buffer, amount, 128, bit16, 2, 1, 1, 0);
	}
	return sndbuffer + amount * (bit16 ? 2 : 1) * pokeys_count;
}

static UBYTE* psg_generate_samples(UBYTE *sndbuffer, int samples)
{
	int ticks;
	int count;
	unsigned int pokeys_count = num_pokeys == 2 ? 2 : 1;
	unsigned int buflen = samples > psg_buffer_length ? psg_buffer_length : samples;
	unsigned int amount = 0;

	/*Log_print("psg_generate_samples %d", buflen);*/
	if (EVIE_version != EVIE_NO)
		while (buflen > 0) {
			ticks = buflen * psg_ticks_per_sample;
			count = AYEMU_calculate_sample(AYEMU_CHIP_EVIE_INDEX, ticks, psg_buffer + amount, buflen);
			amount += count;
			buflen -= count;
		}
	if (amount > 0) {
		if (pokeys_count == 2) {
			if (psg_pan == AYEMU_PSG_PAN_ABC) {
				Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 2, 0);
				Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 1, 2, 1);
			}
			else /*(psg_pan == AYEMU_PSG_PAN_MONO)*/ {
				Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 1, 0);
				Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 1, 1, 0);
			}
		}
		else /*(pokeys_count == 1)*/ { /* in this case psg_pan is always AYEMU_PSG_PAN_MONO */
			Util_mix(sndbuffer, psg_buffer, amount, 128, bit16, pokeys_count, 0, 1, 0);
		}
	}
	return sndbuffer + amount * (bit16 ? 2 : 1) * pokeys_count;
}

static UBYTE* generate_samples(UBYTE *sndbuffer, int samples)
{
	sid_generate_samples(sndbuffer, samples);
	psg_generate_samples(sndbuffer, samples);
	return sndbuffer + samples * (num_pokeys == 2 ? 2 : 1);
}

void EVIE_Process(void *sndbuffer, int sndn)
{
	/*Log_print("Evie_Process");*/

	if (EVIE_version != EVIE_NO) {
		int two_pokeys = (num_pokeys == 2);
		unsigned int sample_size = (two_pokeys ? 2 : 1);
		unsigned int samples_count = sndn / sample_size;
		generate_samples((UBYTE*)sndbuffer, samples_count);
	}
}

#ifdef SYNCHRONIZED_SOUND
#ifndef COUNT_CYCLES
unsigned int EVIE_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;

	/*Log_print("Evie_GenerateSync");*/

	if (EVIE_version != EVIE_NO) {
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
static unsigned int sid_generate_sync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;
	if (EVIE_version != EVIE_NO) {
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
		/*Log_print("Evie_GenerateSync");*/
		/*Log_print("sid_ticks=%d, num_ticks=%d", ticks, num_ticks);*/
		if (ticks > 0) {
			count = RESID_calculate_sample(RESID_CHIP_EVIE_INDEX, ticks, sid_buffer, samples_count);
		}
		/* overgenerate ticks to make lacking sample */
		while (count < samples_count) {
			sid_ticks += sid_ticks_per_tick;
			sid_ticks = modf(sid_ticks, &int_part);
			ticks = int_part;
			if (ticks > 0) {
				unsigned int amount = RESID_calculate_sample(RESID_CHIP_EVIE_INDEX, ticks, sid_buffer + count, 1);
				count += amount;
			}
			overclock++;
		}
		/*unsigned int expected_ticks = samples_count * ticks_per_sample;
		Log_print("over=%d, num=%d, exp=%d, diff=%d", overclock, num_ticks, expected_ticks, num_ticks+overclock-expected_ticks);*/
		sid_ticks -= overclock * sid_ticks_per_tick;
		if (count > 0) {
			Util_mix(buffer, sid_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
			if (pokeys_count == 2)
				Util_mix(buffer, sid_buffer, count, 128, bit16, 2, 1, 1, 0);
			buffer += count * sample_size;
		}
	}
	return buffer - buffer_begin;
}

static unsigned int psg_generate_sync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	UBYTE *buffer = buffer_begin;
	if (EVIE_version != EVIE_NO) {
		double int_part;
		int ticks;
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
		/*Log_print("Evie_GenerateSync");*/
		/*Log_print("psg_ticks=%d, num_ticks=%d", ticks, num_ticks);*/
		if (ticks > 0) {
			count = AYEMU_calculate_sample(AYEMU_CHIP_EVIE_INDEX, ticks, psg_buffer, samples_count);
			/*Log_print("calc_sample %d", count);*/
		}
		/* overgenerate ticks to make lacking sample */
		while (count < samples_count) {
			psg_ticks += psg_ticks_per_tick;
			psg_ticks = modf(psg_ticks, &int_part);
			ticks = int_part;
			if (ticks > 0) {
				unsigned int amount = AYEMU_calculate_sample(AYEMU_CHIP_EVIE_INDEX, ticks, psg_buffer + count, 1);
				/*Log_print("calc_sample_loop %d", amount);*/
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
					Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 2, 0);
					Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 1, 2, 1);
				}
				else /*(psg_pan == AYEMU_PSG_PAN_MONO)*/ {
					Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
					Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 1, 1, 0);
				}
			}
			else /*(pokeys_count == 1)*/ { /* in this case psg_pan is always AYEMU_PSG_PAN_MONO */
				Util_mix(buffer, psg_buffer, count, 128, bit16, pokeys_count, 0, 1, 0);
			}
			buffer += count * sample_size;
		}
	}
	return buffer - buffer_begin;
}

unsigned int EVIE_GenerateSync(UBYTE *buffer_begin, UBYTE *buffer_end, unsigned int num_ticks, unsigned int sndn)
{
	sid_generate_sync(buffer_begin, buffer_end, num_ticks, sndn);
	psg_generate_sync(buffer_begin, buffer_end, num_ticks, sndn);
	return sndn;
}
#endif
#endif /* SYNCHRONIZED_SOUND */

void EVIE_StateSave(void)
{
	/*Log_print("Evie_StateSave");*/

	StateSav_SaveINT(&EVIE_version, 1);
	if (EVIE_version != EVIE_NO) {
		RESID_State sid_state;
		AYEMU_State psg_state;

		RESID_read_state(RESID_CHIP_EVIE_INDEX, &sid_state);

		StateSav_SaveUBYTE(sid_state.sid_register, 0x20);
		StateSav_SaveUBYTE(&sid_state.bus_value, 1);
		StateSav_SaveINT((int*)&sid_state.bus_value_ttl, 1);
		StateSav_SaveINT((int*)sid_state.accumulator, 3);
		StateSav_SaveINT((int*)sid_state.shift_register, 3);
		StateSav_SaveUWORD(sid_state.rate_counter, 3);
		StateSav_SaveUWORD(sid_state.rate_counter_period, 3);
		StateSav_SaveUWORD(sid_state.exponential_counter, 3);
		StateSav_SaveUWORD(sid_state.exponential_counter_period, 3);
		StateSav_SaveUBYTE(sid_state.envelope_counter, 3);
		StateSav_SaveUBYTE((UBYTE*)sid_state.envelope_state, 3);
		StateSav_SaveUBYTE(sid_state.hold_zero, 3);

		AYEMU_read_state(AYEMU_CHIP_EVIE_INDEX, &psg_state);

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

		StateSav_SaveINT(&EVIE_covox_page, 1);

		StateSav_SaveUBYTE(&config, 1);
	}
}

void EVIE_StateRead(void)
{
	/*Log_print("Evie_StateRead");*/

	StateSav_ReadINT(&EVIE_version, 1);
	if (EVIE_version != EVIE_NO) {
		RESID_State sid_state;
		AYEMU_State psg_state;

		StateSav_ReadUBYTE(sid_state.sid_register, 0x20);
		StateSav_ReadUBYTE(&sid_state.bus_value, 1);
		StateSav_ReadINT((int*)&sid_state.bus_value_ttl, 1);
		StateSav_ReadINT((int*)sid_state.accumulator, 3);
		StateSav_ReadINT((int*)sid_state.shift_register, 3);
		StateSav_ReadUWORD(sid_state.rate_counter, 3);
		StateSav_ReadUWORD(sid_state.rate_counter_period, 3);
		StateSav_ReadUWORD(sid_state.exponential_counter, 3);
		StateSav_ReadUWORD(sid_state.exponential_counter_period, 3);
		StateSav_ReadUBYTE(sid_state.envelope_counter, 3);
		StateSav_ReadUBYTE((UBYTE*)sid_state.envelope_state, 3);
		StateSav_ReadUBYTE(sid_state.hold_zero, 3);

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

		StateSav_ReadINT(&EVIE_covox_page, 1);

		StateSav_ReadUBYTE(&config, 1);
		update_config(config);

		evie_initialize(main_freq, dsprate, num_pokeys, bit16, &sid_state, &psg_state);
	}
	else
		evie_initialize(main_freq, dsprate, num_pokeys, bit16, NULL, NULL);
}

/*
vim:ts=4:sw=4:
*/
