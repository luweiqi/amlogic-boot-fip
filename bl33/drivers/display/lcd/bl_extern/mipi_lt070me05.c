/*
 * drivers/display/lcd/lcd_extern/mipi_lt070me05.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <common.h>
#include <malloc.h>
#include <asm/arch/gpio.h>
#ifdef CONFIG_OF_LIBFDT
#include <libfdt.h>
#endif
#include <amlogic/aml_lcd.h>
#include <amlogic/aml_bl_extern.h>
#include "bl_extern.h"
#include "../aml_lcd_common.h"


#define BL_EXTERN_INDEX			1
#define BL_EXTERN_NAME			"mipi_lt070me05"
#define BL_EXTERN_TYPE			BL_EXTERN_MIPI

static unsigned int bl_status = 0;

static int mipi_lt070me05_power_on(void)
{
	bl_status = 1;

	return 0;
}

static int mipi_lt070me05_power_off(void)
{
	bl_status = 0;

	return 0;
}

/******************** mipi command ********************
 *format:  data_type, num, data....
 *special: data_type=0xff, num<0xff means delay ms, num=0xff means ending.
 */
static int mipi_lt070me05_set_level(unsigned int level)
{
	unsigned char payload[] = {0x15, 2, 0x51, 0xe6, 0xff, 0xff};
	struct aml_lcd_drv_s *bl_drv = aml_lcd_get_driver();
	struct aml_bl_extern_driver_s *bl_extern = aml_bl_extern_get_driver();
	unsigned int level_max, level_min;
	unsigned int dim_max, dim_min;

	if (bl_drv == NULL)
		return -1;
	level_max = bl_drv->bl_config->level_max;
	level_min = bl_drv->bl_config->level_min;
	dim_max = bl_extern->config->dim_max;
	dim_min = bl_extern->config->dim_min;
	level = dim_min - ((level - level_min) * (dim_min - dim_max)) /
			(level_max - level_min);
	level &= 0xff;

	if (bl_status) {
		payload[3] = level;
#ifdef CONFIG_AML_LCD_TABLET
		dsi_write_cmd(&payload[0]);
#endif
	}
	return 0;
}

static int mipi_lt070me05_update(void)
{
	struct aml_bl_extern_driver_s *bl_extern = aml_bl_extern_get_driver();

	if (bl_extern == NULL) {
		BLEXERR("%s driver is null\n", BL_EXTERN_NAME);
		return -1;
	}

	if (bl_extern->config->type == BL_EXTERN_MAX) {
		bl_extern->config->index = BL_EXTERN_INDEX;
		bl_extern->config->type = BL_EXTERN_TYPE;
		strcpy(bl_extern->config->name, BL_EXTERN_NAME);
	}

	bl_extern->device_power_on = mipi_lt070me05_power_on;
	bl_extern->device_power_off = mipi_lt070me05_power_off;
	bl_extern->device_bri_update = mipi_lt070me05_set_level;

	return 0;
}

int mipi_lt070me05_probe(void)
{
	int ret = 0;

	ret = mipi_lt070me05_update();

	BLEX("%s: %d\n", __func__, ret);

	return ret;
}


