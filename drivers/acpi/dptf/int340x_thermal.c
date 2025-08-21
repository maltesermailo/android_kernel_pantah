// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI support for int340x thermal drivers
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Zhang Rui <rui.zhang@intel.com>
 */

#include <linux/acpi.h>
#include <linux/module.h>

#include "../fan.h"
#include "../int340x_thermal.h"
#include "../internal.h"


#define INT3401_DEVICE 0X01
static const struct acpi_device_id int340x_thermal_device_ids[] = {
	ACPI_FAN_DEVICE_IDS,
	ACPI_INT3400_DEVICE_IDS,
	ACPI_INT3401_DEVICE_IDS,
	ACPI_INT3402_DEVICE_IDS,
	ACPI_INT3403_DEVICE_IDS,
	ACPI_INT3406_DEVICE_IDS,
	ACPI_INT3407_DEVICE_IDS,
	ACPI_PCH_FIVR_DEVICE_IDS,
	{""},
};

static int int340x_thermal_handler_attach(struct acpi_device *adev,
					const struct acpi_device_id *id)
{
	// TODO: check topology of thermal devices, especially relation between int3400 and int3403
	// TODO: check THERMAL_INTEL_SOC_DTS

	/* Do not attach int340x devices unless platform driver is loaded */
	return 1;
}

static struct acpi_scan_handler int340x_thermal_handler = {
	.ids = int340x_thermal_device_ids,
	.attach = int340x_thermal_handler_attach,
};

void __init acpi_int340x_thermal_init(void)
{
	acpi_scan_add_handler(&int340x_thermal_handler);
}
