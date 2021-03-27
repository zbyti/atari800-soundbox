/*
 * resid.cc - reSID interface
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

#include <resid/sid.h> 

#include "resid.h"

extern "C" {

#include "util.h"
#include "statesav.h"
#include "log.h"


int RESID_resample_method = RESID_SYNTHESIS_METHOD_RESAMPLE_INTERPOLATE;

static SID *sid[] = {
	NULL,	/* SlightSID left */
	NULL,	/* SlightSID right */
	NULL,	/* Evie */
	NULL,	/* SIDari left */
	NULL,	/* SIDari right */
};

static const int autochoose_order_resample_method[] = { 0, 1, 2, 3,
                                                 -1 };
static const int cfg_vals[] = {
	/* resample method */
	RESID_SYNTHESIS_METHOD_RESAMPLE_INTERPOLATE,
	RESID_SYNTHESIS_METHOD_RESAMPLE_FAST,
	RESID_SYNTHESIS_METHOD_INTERPOLATE,
	RESID_SYNTHESIS_METHOD_FAST
};
static const char * cfg_strings[] = {
	/* resample method */
	"INTERPOLATE-RESAMPLE",
	"FAST-RESAMPLE",
	"INTERPOLATE",
	"FAST"
};

void RESID_open(int sid_index)
{
	sid[sid_index] = new SID();
}

void RESID_close(int sid_index)
{
	if (sid[sid_index] != NULL) {
		delete sid[sid_index];
		sid[sid_index] = NULL;
	}
}

int RESID_is_opened(int sid_index)
{
	return sid[sid_index] != NULL;
}

int RESID_init(int sid_index, double cycles_per_sec, int sid_model, double sample_rate)
{
	chip_model model;
	switch (sid_model) {
	case RESID_SID_MODEL_8580:
	case RESID_SID_FILTER_LINEAR:
		model = MOS8580;
		break;
	case RESID_SID_MODEL_6581:
	case RESID_SID_FILTER_NONE:
	default:
		model = MOS6581;
	}

	sampling_method method;
	switch (RESID_resample_method) {
	case RESID_SYNTHESIS_METHOD_FAST:
		method = SAMPLE_FAST;
		break;
	case RESID_SYNTHESIS_METHOD_INTERPOLATE:
		method = SAMPLE_INTERPOLATE;
		break;
	case RESID_SYNTHESIS_METHOD_RESAMPLE_FAST:
		method = SAMPLE_RESAMPLE_FAST;
		break;
	case RESID_SYNTHESIS_METHOD_RESAMPLE_INTERPOLATE:
	default:
		method = SAMPLE_RESAMPLE_INTERPOLATE;
	}

	sid[sid_index]->set_chip_model(model);
	sid[sid_index]->enable_filter(sid_model != RESID_SID_FILTER_NONE);
	sid[sid_index]->enable_external_filter(true);
	int result = sid[sid_index]->set_sampling_parameters(cycles_per_sec, method, sample_rate);
	return result;
}

UBYTE RESID_read(int sid_index, UBYTE addr)
{
	return sid[sid_index]->read(addr);
}

void RESID_write(int sid_index, UBYTE addr, UBYTE byte)
{
	sid[sid_index]->write(addr, byte);
}

void RESID_reset(int sid_index)
{
	sid[sid_index]->reset();
}

void RESID_input(int sid_index, int sample)
{
	sid[sid_index]->input(sample);
}

int RESID_calculate_sample(int sid_index, int delta, SWORD *buf, int nr)
{
	return sid[sid_index]->clock(delta, buf, nr);
}

void RESID_read_state(int sid_index, RESID_State *state)
{
	SID::State sid_state = sid[sid_index]->read_state();
	for (int i = 0; i < 0x20; i++)
		state->sid_register[i] = sid_state.sid_register[i];
	state->bus_value = sid_state.bus_value;
	state->bus_value_ttl = sid_state.bus_value_ttl;
	for (int i = 0; i < 3; i++) {
		state->accumulator[i] = sid_state.accumulator[i];
		state->shift_register[i] = sid_state.shift_register[i];
		state->rate_counter[i] = sid_state.rate_counter[i];
		state->rate_counter_period[i] = sid_state.rate_counter_period[i];
		state->exponential_counter[i] = sid_state.exponential_counter[i];
		state->exponential_counter_period[i] = sid_state.exponential_counter_period[i];
		state->envelope_counter[i] = sid_state.envelope_counter[i];
		state->envelope_state[i] = (RESID_Envelope)sid_state.envelope_state[i];
		state->hold_zero[i] = sid_state.hold_zero[i];
	}
}

void RESID_write_state(int sid_index, RESID_State *state)
{
	SID::State sid_state;
	for (int i = 0; i < 0x20; i++)
		sid_state.sid_register[i] = state->sid_register[i];
	sid_state.bus_value = state->bus_value;
	sid_state.bus_value_ttl = state->bus_value_ttl;
	for (int i = 0; i < 3; i++) {
		sid_state.accumulator[i] = state->accumulator[i];
		sid_state.shift_register[i] = state->shift_register[i];
		sid_state.rate_counter[i] = state->rate_counter[i];
		sid_state.rate_counter_period[i] = state->rate_counter_period[i];
		sid_state.exponential_counter[i] = state->exponential_counter[i];
		sid_state.exponential_counter_period[i] = state->exponential_counter_period[i];
		sid_state.envelope_counter[i] = state->envelope_counter[i];
		sid_state.envelope_state[i] = (EnvelopeGenerator::State)state->envelope_state[i];
		sid_state.hold_zero[i] = state->hold_zero[i];
	}
	sid[sid_index]->write_state(sid_state);
}


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

int RESID_Initialise(int *argc, char *argv[])
{
	int i, j;
	int help_only = FALSE;
	for (i = j = 1; i < *argc; i++) {
		int i_a = (i + 1 < *argc); /* is argument available? */
		int a_m = FALSE; /* error, argument missing! */
		int a_i = FALSE; /* error, argument invalid! */

		if (strcmp(argv[i], "-sid-resample-method") == 0) {
			if (i_a) {
				if (!MatchParameter(argv[++i], autochoose_order_resample_method, &RESID_resample_method))
					a_i = TRUE;
			}
			else a_m = TRUE;
		}
		else {
		 	if (strcmp(argv[i], "-help") == 0) {
		 		help_only = TRUE;
				Log_print("\t-sid-resample-method interpolate-resample|fast-resample|interpolate|fast");
				Log_print("\t                 Select resample method for SID emulation");
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

	return TRUE;
}

int RESID_ReadConfig(char *string, char *ptr)
{
	if (strcmp(string, "SID_RESAMPLE_METHOD") == 0) {
		if (!MatchParameter(ptr, autochoose_order_resample_method, &RESID_resample_method))
			return FALSE;
	}
	else return FALSE; /* no match */
	return TRUE; /* matched something */
}

void RESID_WriteConfig(FILE *fp)
{
	fprintf(fp, "SID_RESAMPLE_METHOD=%s\n", MatchValue(autochoose_order_resample_method, &RESID_resample_method));
}

void RESID_StateSave(void)
{
	StateSav_SaveINT(&RESID_resample_method, 1);
}

void RESID_StateRead(void)
{
	StateSav_ReadINT(&RESID_resample_method, 1);
}

} /* extern "C" */
/*
vim:ts=4:sw=4:
*/

