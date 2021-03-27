/*
 * ymf262.c - OPL3 interface
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

#include <stdlib.h>

#include "opl.h"
#include "ymf262.h"

#include "util.h"
#include "statesav.h"
#include "log.h"


static int opl3 = FALSE;
static double last_sample_rate = 0.0;

void YMF262_open(int opl3_index)
{
	opl3 = TRUE;
}

void YMF262_close(int opl3_index)
{
	if (opl3) {
		opl3 = FALSE;
	}
}

int YMF262_is_opened(int opl3_index)
{
	return opl3;
}

void YMF262_init(int opl3_index, double cycles_per_sec, double sample_rate)
{
	adlib_init(sample_rate);
	last_sample_rate = sample_rate;
}

UBYTE YMF262_read(int opl3_index, double tick)
{
	return adlib_reg_read(0, tick);
}

void YMF262_write(int opl3_index, UWORD addr, UBYTE byte, double tick)
{
	if (addr & 1)
		/*WARN: opl_index is internal of opl.h! */
		adlib_write(opl_index, byte, tick);
	else
		adlib_write_index(addr, byte);
}

void YMF262_reset(int opl3_index)
{
	if (last_sample_rate)
		adlib_init(last_sample_rate);
}

int YMF262_calculate_sample(int opl3_index, int delta, SWORD *buf, int nr)
{
	adlib_getsample(buf, nr);
	return nr;
}

void YMF262_read_state(int opl3_index, YMF262_State *state)
{
	unsigned int i;
	for (i = 0; i < 0x200; i++) {
		state->regs[i] = adlibreg[i];
	}
}

void YMF262_write_state(int opl3_index, YMF262_State *state)
{
	unsigned int i;
	for (i = 0; i < 0x200; i++) {
		adlibreg[i] = state->regs[i];
	}
}

/*
vim:ts=4:sw=4:
*/
