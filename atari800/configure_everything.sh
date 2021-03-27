#!/bin/bash
./configure \
	--enable-monitorbreakpoints \
	--enable-monitorprofile \
	--enable-monitortrace \
	--enable-seriosound \
	--enable-volonlysound \
	--enable-synchronized_sound \
	--enable-sid_emulation \
	--enable-psg_emulation \
	--enable-opl3_emulation \
	"$@"
