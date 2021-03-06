/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2012 secunet Security Networks AG
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

#include <stdint.h>
#include <stdlib.h>
#include <arch/io.h>
//#include <pc80/mc146818rtc.h>
#include <device/device.h>
#include <console/console.h>
#include <drivers/intel/gma/int15.h>
#include <pc80/keyboard.h>
#include <ec/acpi/ec.h>
#include <smbios.h>
#include <string.h>
#include <build.h>
#include <ec/lenovo/pmh7/pmh7.h>
#include <ec/acpi/ec.h>
#include <ec/lenovo/h8/h8.h>
#include <device/azalia_device.h>

#include "hda_verb.h"

#if CONFIG_GENERATE_ACPI_TABLES
#include "cstates.c" /* Include it, as the linker won't find
			the overloaded weak function in there. */
#endif

static void verb_setup(void)
{
	cim_verb_data = mainboard_cim_verb_data;
	cim_verb_data_size = sizeof(mainboard_cim_verb_data);
	pc_beep_verbs = mainboard_pc_beep_verbs;
	pc_beep_verbs_size = ARRAY_SIZE(mainboard_pc_beep_verbs);
}

const char *smbios_mainboard_bios_version(void)
{
	/* Satisfy thinkpad_acpi.  */
	if (strlen(CONFIG_LOCALVERSION))
		return "CBET4000 " CONFIG_LOCALVERSION;
	else
		return "CBET4000 " COREBOOT_VERSION;
}

static void mainboard_init(device_t dev)
{
	/* This sneaked in here, because X200 SuperIO chip isn't really
	   connected to anything and hence we don't init it.
	 */
	pc_keyboard_init();
}

static void mainboard_enable(device_t dev)
{
	verb_setup();
	install_intel_vga_int15_handler(GMA_INT15_ACTIVE_LFP_INT_LVDS, GMA_INT15_PANEL_FIT_CENTERING, GMA_INT15_BOOT_DISPLAY_DEFAULT, 2);

	dev->ops->init = mainboard_init;
}

struct chip_operations mainboard_ops = {
	.enable_dev = mainboard_enable,
};

