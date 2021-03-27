/*
 * ayemu.c - PSG interface
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

#include <ayemu.h> 
#include <stdlib.h>

#include "psgemu.h"

#include "util.h"
#include "statesav.h"
#include "log.h"


static ayemu_ay_t *psg[] = {
	NULL,	/* Evie */
	NULL,	/* SONari left */
	NULL,	/* SONari right */
	NULL,	/* Melody left */
	NULL,	/* Melody right */
};
static ayemu_ay_reg_frame_t *reg[] = {
	NULL,	/* Evie */
	NULL,	/* SONari left */
	NULL,	/* SONari right */
	NULL,	/* Melody left */
	NULL,	/* Melody right */
};

void AYEMU_open(int psg_index)
{
	psg[psg_index] = Util_malloc(sizeof(ayemu_ay_t));
	reg[psg_index] = Util_malloc(sizeof(ayemu_ay_reg_frame_t));
}

void AYEMU_close(int psg_index)
{
	if (psg[psg_index] != NULL) {
		free(psg[psg_index]);
		psg[psg_index] = NULL;
		free(reg[psg_index]);
		reg[psg_index] = NULL;
	}
}

int AYEMU_is_opened(int psg_index)
{
	return psg[psg_index] != NULL;
}

void AYEMU_init(int psg_index, double cycles_per_sec, int psg_model, int psg_pan, double sample_rate)
{
	ayemu_chip_t model;
	ayemu_stereo_t stereo;
	ayemu_ay_t *chip = psg[psg_index];
	ayemu_ay_reg_frame_t *regs = reg[psg_index];

	switch (psg_model) {
	case AYEMU_PSG_MODEL_AY:
		model = AYEMU_AY;
		break;
	case AYEMU_PSG_MODEL_YM:
	default:
		model = AYEMU_YM;
	}

	switch (psg_pan) {
	case AYEMU_PSG_PAN_ACB:
		stereo = AYEMU_ACB;
		break;
	case AYEMU_PSG_PAN_ABC:
		stereo = AYEMU_ABC;
		break;
	case AYEMU_PSG_PAN_MONO:
	default:
		stereo = AYEMU_MONO;
	}

	ayemu_init(chip);
	ayemu_set_chip_type(chip, model, NULL);
	ayemu_set_chip_freq(chip, cycles_per_sec);
	ayemu_set_stereo(chip, stereo, NULL);
	ayemu_set_sound_format(chip, sample_rate, stereo != AYEMU_MONO ? 2 : 1, 16);
	memset(regs, 0, 14);
	ayemu_set_regs(chip, *regs);
}

UBYTE AYEMU_read(int psg_index, UBYTE addr)
{
	if (addr < 14) {
		ayemu_ay_reg_frame_t *regs = reg[psg_index];
		return (*regs)[addr];
	}
	return 0xff;
}

static UBYTE ay_reg_mask[14] = {
	0xff, 0x0f,
	0xff, 0x0f,
	0xff, 0x0f,
	0x1f,
	0xff,
	0x1f, 0x1f, 0x1f,
	0xff, 0xff,
	0x0f
};

void AYEMU_write(int psg_index, UBYTE addr, UBYTE byte)
{
	if (addr < 14) {
		ayemu_ay_reg_frame_t *regs = reg[psg_index];
		(*regs)[addr] = byte & ay_reg_mask[addr];
		if (addr == 13) {
			ayemu_set_regs(psg[psg_index], *regs);
		}
		else {
			UBYTE val = (*regs)[13];
			(*regs)[13] = 0xff; /* prevents from reset envelope */
			ayemu_set_regs(psg[psg_index], *regs);
			(*regs)[13] = val;
		}
	}
}

void AYEMU_reset(int psg_index)
{
	ayemu_ay_t *chip = psg[psg_index];
	ayemu_ay_reg_frame_t *regs = reg[psg_index];

	ayemu_reset(chip);
	memset(regs, 0, 14);
	ayemu_set_regs(chip, *regs);
}

int AYEMU_calculate_sample(int psg_index, int delta, SWORD *buf, int nr)
{
	ayemu_ay_t *chip = psg[psg_index];
	SWORD *next = (SWORD *)ayemu_gen_sound(chip, buf, nr * chip->sndfmt.channels * sizeof(SWORD));
	return (int)(next - buf) / chip->sndfmt.channels;
}

void AYEMU_read_state(int psg_index, AYEMU_State *state)
{
	int i, j;
	ayemu_ay_t *psg_state = psg[psg_index];
	ayemu_ay_reg_frame_t *psg_regs = reg[psg_index];

	for (i = 0; i < 32; i++)
		state->table[i] = psg_state->table[i];
	state->type = psg_state->type;
	state->chip_freq = psg_state->ChipFreq;
	for (i = 0; i < 6; i++)
		state->eq[i] = psg_state->eq[i];

	state->tone_a = psg_state->regs.tone_a;
	state->tone_b = psg_state->regs.tone_b;
	state->tone_c = psg_state->regs.tone_c;
	state->noise = psg_state->regs.noise;
	state->R7_tone_a = psg_state->regs.R7_tone_a;
	state->R7_tone_b = psg_state->regs.R7_tone_b;
	state->R7_tone_c = psg_state->regs.R7_tone_c;
	state->R7_noise_a = psg_state->regs.R7_noise_a;
	state->R7_noise_b = psg_state->regs.R7_noise_b;
	state->R7_noise_c = psg_state->regs.R7_noise_c;
	state->vol_a = psg_state->regs.vol_a;
	state->vol_b = psg_state->regs.vol_b;
	state->vol_c = psg_state->regs.vol_c;
	state->env_a = psg_state->regs.env_a;
	state->env_b = psg_state->regs.env_b;
	state->env_c = psg_state->regs.env_c;
	state->env_freq = psg_state->regs.env_freq;	
	state->env_style = psg_state->regs.env_style;

	state->freq = psg_state->sndfmt.freq;
	state->channels = psg_state->sndfmt.channels;	
	state->bpc = psg_state->sndfmt.bpc;

	state->magic = psg_state->magic;
	state->default_chip_flag = psg_state->default_chip_flag;
	state->default_stereo_flag = psg_state->default_stereo_flag;
	state->default_sound_format_flag = psg_state->default_sound_format_flag;
	state->dirty = psg_state->dirty;

	state->bit_a = psg_state->bit_a;
	state->bit_b = psg_state->bit_b;
	state->bit_c = psg_state->bit_c;
	state->bit_n = psg_state->bit_n;
	state->cnt_a = psg_state->cnt_a;
	state->cnt_b = psg_state->cnt_b;
	state->cnt_c = psg_state->cnt_c;
	state->cnt_n = psg_state->cnt_n;
	state->cnt_e = psg_state->cnt_e;
	state->chip_tacts_per_outcount = psg_state->ChipTacts_per_outcount;
	state->amp_global = psg_state->Amp_Global;
	for (i = 0; i < 6; i++)
		for (j = 0; j < 32; j++)
			state->vols[i][j] = psg_state->vols[i][j];
	state->env_num = psg_state->EnvNum;
	state->env_pos = psg_state->env_pos;
	state->cur_seed = psg_state->Cur_Seed;

	for (i = 0; i < 14; i++)
		state->regs[i] = (*psg_regs)[i];
}

void AYEMU_write_state(int psg_index, AYEMU_State *state)
{
	int i, j;
	ayemu_ay_t *psg_state = psg[psg_index];
	ayemu_ay_reg_frame_t *psg_regs = reg[psg_index];

	for (i = 0; i < 32; i++)
		psg_state->table[i] = state->table[i];
	psg_state->type = state->type;
	psg_state->ChipFreq = state->chip_freq;
	for (i = 0; i < 6; i++)
		psg_state->eq[i] = state->eq[i];

	psg_state->regs.tone_a = state->tone_a;
	psg_state->regs.tone_b = state->tone_b;
	psg_state->regs.tone_c = state->tone_c;
	psg_state->regs.noise = state->noise;
	psg_state->regs.R7_tone_a = state->R7_tone_a;
	psg_state->regs.R7_tone_b = state->R7_tone_b;
	psg_state->regs.R7_tone_c = state->R7_tone_c;
	psg_state->regs.R7_noise_a = state->R7_noise_a;
	psg_state->regs.R7_noise_b = state->R7_noise_b;
	psg_state->regs.R7_noise_c = state->R7_noise_c;
	psg_state->regs.vol_a = state->vol_a;
	psg_state->regs.vol_b = state->vol_b;
	psg_state->regs.vol_c = state->vol_c;
	psg_state->regs.env_a = state->env_a;
	psg_state->regs.env_b = state->env_b;
	psg_state->regs.env_c = state->env_c;
	psg_state->regs.env_freq = state->env_freq;	
	psg_state->regs.env_style = state->env_style;

	psg_state->sndfmt.freq = state->freq;
	psg_state->sndfmt.channels = state->channels;	
	psg_state->sndfmt.bpc = state->bpc;

	psg_state->magic = state->magic;
	psg_state->default_chip_flag = state->default_chip_flag;
	psg_state->default_stereo_flag = state->default_stereo_flag;
	psg_state->default_sound_format_flag = state->default_sound_format_flag;
	psg_state->dirty = state->dirty;

	psg_state->bit_a = state->bit_a;
	psg_state->bit_b = state->bit_b;
	psg_state->bit_c = state->bit_c;
	psg_state->bit_n = state->bit_n;
	psg_state->cnt_a = state->cnt_a;
	psg_state->cnt_b = state->cnt_b;
	psg_state->cnt_c = state->cnt_c;
	psg_state->cnt_n = state->cnt_n;
	psg_state->cnt_e = state->cnt_e;
	psg_state->ChipTacts_per_outcount = state->chip_tacts_per_outcount;
	psg_state->Amp_Global = state->amp_global;
	for (i = 0; i < 6; i++)
		for (j = 0; j < 32; j++)
			psg_state->vols[i][j] = state->vols[i][j];
	psg_state->EnvNum = state->env_num;
	psg_state->env_pos = state->env_pos;
	psg_state->Cur_Seed = state->cur_seed;

	for (i = 0; i < 14; i++)
		(*psg_regs)[i] = state->regs[i];
}

/*
vim:ts=4:sw=4:
*/
