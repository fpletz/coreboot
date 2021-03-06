/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2007-2009 coresystems GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <types.h>
#include <device/device.h>
#include <console/console.h>
#include <drivers/intel/gma/int15.h>
#include <pc80/mc146818rtc.h>
#include <arch/io.h>
#include <arch/interrupt.h>
#include <device/azalia_device.h>
#include "superio_hwm.h"

/* Audio Setup */

static void verb_setup(void)
{
	// Default VERB is fine on this mainboard.
	cim_verb_data = NULL;
	cim_verb_data_size = 0;
}

// mainboard_enable is executed as first thing after
// enumerate_buses().

static void mainboard_enable(device_t dev)
{
	install_intel_vga_int15_handler(GMA_INT15_ACTIVE_LFP_INT_LVDS, GMA_INT15_PANEL_FIT_DEFAULT, GMA_INT15_BOOT_DISPLAY_DEFAULT, 3);
	verb_setup();
	hwm_setup();
}

struct chip_operations mainboard_ops = {
	.enable_dev = mainboard_enable,
};
