/*
 * drivers/display/lcd/lcd_extern/mipi_P070ACB.c
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
#include <amlogic/aml_lcd_extern.h>
#include "lcd_extern.h"

#define LCD_EXTERN_INDEX		2
#define LCD_EXTERN_NAME			"mipi_P070ACB"
#define LCD_EXTERN_TYPE			LCD_EXTERN_MIPI

//******************** mipi command ********************//
//format:  data_type, num, data....
//special: data_type=0xff, num<0xff means delay ms, num=0xff means ending.
//******************************************************//
static unsigned char mipi_init_on_table[] = {
	0x29, 5, 0xFF, 0xAA, 0x55, 0x25, 0x01,
	0x23, 2, 0xFC, 0x08,
	0xFF, 1,	   /* delay(ms) */
	0x23, 2, 0xFC, 0x00,

	0xFF, 1,	   /* delay(ms) */
	0x23, 2, 0x6F, 0x21,
	0x23, 2, 0xF7, 0x01,
	0xFF, 1,	   /* delay(ms) */
	0x23, 2, 0x6F, 0x21,
	0x23, 2, 0xF7, 0x00,
	0xFF, 1,	   /* delay(ms) */

	0x23, 2, 0x6F, 0x1A,
	0x23, 2, 0xF7, 0x05,
	0xFF, 1,	   /* delay(ms) */

	0x29, 5, 0xFF, 0xAA, 0x55, 0x25, 0x00,

	0x29, 6, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00,
	0x29, 3, 0xB1, 0x68, 0x41,
	0x23, 2, 0xB5, 0x88,
	0x29, 6, 0xBD, 0x02, 0xB0, 0x0C, 0x14, 0x00,
	0x23, 2, 0xC8, 0x80,

	0x29, 6, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01,
	0x29, 3, 0xB3, 0x2D, 0x2D,
	0x29, 3, 0xB4, 0x19, 0x19,
	0x23, 2, 0xB5, 0x06,

	0x29, 3, 0xB9, 0x36, 0x36,
	0x29, 3, 0xBA, 0x26, 0x26,
	0x29, 3, 0xBC, 0xA8, 0x01,
	0x29, 3, 0xBD, 0xAB, 0x01,
	0x23, 2, 0xC0, 0x0C,

	0x29, 6, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x02,
	0x23, 2, 0xEE, 0x02,
	0x29, 7, 0xB0, 0x00, 0x50, 0x00, 0x52, 0x00, 0x73,
	0x23, 2, 0x6F, 0x06,
	0x29, 7, 0xB0, 0x00, 0x8F, 0x00, 0xA5, 0x00, 0xCA,
	0x23, 2, 0x6F, 0x0C,
	0x29, 5, 0xB0, 0x00, 0xEA, 0x01, 0x1B,
	0x29, 7, 0xB1, 0x01, 0x42, 0x01, 0x82, 0x01, 0xB3,
	0x23, 2, 0x6F, 0x06,
	0x29, 7, 0xB1, 0x02, 0x00, 0x02, 0x41, 0x02, 0x42,
	0x23, 2, 0x6F, 0x0C,
	0x29, 5, 0xB1, 0x02, 0x78, 0x02, 0xB5,
	0x29, 7, 0xB2, 0x02, 0xDA, 0x03, 0x12, 0x03, 0x3A,
	0x23, 2, 0x6F, 0x06,
	0x29, 7, 0xB2, 0x03, 0x6E, 0x03, 0x8D, 0x03, 0xB1,
	0x23, 2, 0x6F, 0x0C,
	0x29, 5, 0xB2, 0x03, 0xCA, 0x03, 0xE8,
	0x29, 5, 0xB3, 0x03, 0xF4, 0x03, 0xFF,

	0x29, 7, 0xBC, 0x00, 0x05, 0x00, 0x52, 0x00, 0x73,
	0x23, 2, 0x6F, 0x06,
	0x29, 7, 0xBC, 0x00, 0x8F, 0x00, 0xA5, 0x00, 0xCA,
	0x23, 2, 0x6F, 0x0C,
	0x29, 5, 0xBC, 0x00, 0xEA, 0x01, 0x1B,
	0x29, 7, 0xBD, 0x01, 0x42, 0x01, 0x82, 0x01, 0xB3,
	0x23, 2, 0x6F, 0x06,
	0x29, 7, 0xBD, 0x02, 0x00, 0x02, 0x41, 0x02, 0x42,
	0x23, 2, 0x6F, 0x0C,
	0x29, 5, 0xBD, 0x02, 0x78, 0x02, 0xB5,
	0x29, 7, 0xBE, 0x02, 0xDA, 0x03, 0x12, 0x03, 0x3A,
	0x23, 2, 0x6F, 0x06,
	0x29, 7, 0xBE, 0x03, 0x6E, 0x03, 0x8D, 0x03, 0xB1,
	0x23, 2, 0x6F, 0x0C,
	0x29, 5, 0xBE, 0x03, 0xCA, 0x03, 0xE8,
	0x29, 5, 0xBF, 0x03, 0xF4, 0x03, 0xFF,

	0x29, 6, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x03,
	0x29, 6, 0xB2, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x29, 6, 0xB6, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x29, 6, 0xB7, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x29, 6, 0xBA, 0x57, 0x00, 0x00, 0x00, 0x00,
	0x29, 6, 0xBB, 0x57, 0x00, 0x00, 0x00, 0x00,
	0x29, 5, 0xC0, 0x00, 0x34, 0x00, 0x00,
	0x29, 5, 0xC1, 0x00, 0x00, 0x34, 0x00,
	0x23, 2, 0xC4, 0x40,

	0x29, 6, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x05,
	0x29, 3, 0xB0, 0x17, 0x06,
	0x29, 3, 0xB1, 0x17, 0x06,
	0x29, 3, 0xB2, 0x17, 0x06,
	0x29, 3, 0xB3, 0x17, 0x06,
	0x29, 3, 0xB4, 0x17, 0x06,

	0x29, 6, 0xBD, 0x03, 0x01, 0x03, 0x03, 0x01,
	0x23, 2, 0xC0, 0x05,
	0x23, 2, 0xC4, 0x82,
	0x23, 2, 0xC5, 0xA2,
	0x29, 3, 0xC8, 0x03, 0x30,
	0x29, 3, 0xC9, 0x03, 0x31,
	0x29, 4, 0xCC, 0x00, 0x00, 0x3C,
	0x29, 4, 0xCD, 0x00, 0x00, 0x3C,
	0x29, 6, 0xD1, 0x00, 0x44, 0x09, 0x00, 0x00,
	0x29, 6, 0xD2, 0x00, 0x04, 0x0B, 0x00, 0x00,

	0x29, 6, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x06,
	0x29, 3, 0xB0, 0x0B, 0x2D,
	0x29, 3, 0xB1, 0x2D, 0x09,
	0x29, 3, 0xB2, 0x2A, 0x29,
	0x29, 3, 0xB3, 0x34, 0x1B,
	0x29, 3, 0xB4, 0x19, 0x17,
	0x29, 3, 0xB5, 0x15, 0x13,
	0x29, 3, 0xB6, 0x11, 0x01,
	0x29, 3, 0xB7, 0x34, 0x34,
	0x29, 3, 0xB8, 0x34, 0x2D,
	0x29, 3, 0xB9, 0x2D, 0x34,
	0x29, 3, 0xBA, 0x2D, 0x2D,
	0x29, 3, 0xBB, 0x34, 0x34,
	0x29, 3, 0xBC, 0x34, 0x34,
	0x29, 3, 0xBD, 0x00, 0x10,
	0x29, 3, 0xBE, 0x12, 0x14,
	0x29, 3, 0xBF, 0x16, 0x18,

	0x29, 3, 0xC0, 0x1A, 0x34,
	0x29, 3, 0xC1, 0x29, 0x2A,
	0x29, 3, 0xC2, 0x08, 0x2D,
	0x29, 3, 0xC3, 0x2D, 0x0A,
	0x29, 3, 0xC4, 0x0A, 0x2D,
	0x29, 3, 0xC5, 0x2D, 0x00,
	0x29, 3, 0xC6, 0x2A, 0x29,
	0x29, 3, 0xC7, 0x34, 0x14,
	0x29, 3, 0xC8, 0x16, 0x18,
	0x29, 3, 0xC9, 0x1A, 0x10,
	0x29, 3, 0xCA, 0x12, 0x08,
	0x29, 3, 0xCB, 0x34, 0x34,
	0x29, 3, 0xCC, 0x34, 0x2D,
	0x29, 3, 0xCD, 0x2D, 0x34,
	0x29, 3, 0xCE, 0x2D, 0x2D,
	0x29, 3, 0xCF, 0x34, 0x34,

	0x29, 3, 0xD0, 0x34, 0x34,
	0x29, 3, 0xD1, 0x09, 0x13,
	0x29, 3, 0xD2, 0x11, 0x1B,
	0x29, 3, 0xD3, 0x19, 0x17,
	0x29, 3, 0xD4, 0x15, 0x34,
	0x29, 3, 0xD5, 0x29, 0x2A,
	0x29, 3, 0xD6, 0x01, 0x2D,
	0x29, 3, 0xD7, 0x2D, 0x0B,
	0x29, 6, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x29, 6, 0xD9, 0x00, 0x00, 0x00, 0x00, 0x00,

	0x29, 3, 0xE5, 0x34, 0x34,
	0x29, 3, 0xE6, 0x34, 0x34,
	0x23, 2, 0xE7, 0x00,
	0x29, 3, 0xE8, 0x34, 0x34,
	0x29, 3, 0xE9, 0x34, 0x34,
	0x23, 2, 0xEA, 0x00,

	0x29, 6, 0xF0, 0x55, 0xAA, 0x52, 0x00, 0x00,

	0x13, 1, 0x35,
	0x13, 1, 0x11,
	0xFF, 120,	   /* delay(ms) */
	0x13, 1, 0x29,
	0xFF, 20,	   /* delay(ms) */
	0xFF, 0xFF,   /* ending flag */

};

static unsigned char mipi_init_off_table[] = {
	0x05, 1, 0x28, /* display off */
	0xFF, 10,      /* delay 10ms */
	0x05, 1, 0x10, /* sleep in */
	0xFF, 150,      /* delay 150ms */
	0xFF, 0xFF,   /* ending flag */
};

static int lcd_extern_driver_update(struct aml_lcd_extern_driver_s *ext_drv)
{
	if (ext_drv == NULL) {
		EXTERR("%s driver is null\n", LCD_EXTERN_NAME);
		return -1;
	}

	if (ext_drv->config->type == LCD_EXTERN_MAX) { //default for no dt
		ext_drv->config->index = LCD_EXTERN_INDEX;
		ext_drv->config->type = LCD_EXTERN_TYPE;
		strcpy(ext_drv->config->name, LCD_EXTERN_NAME);
	}
	ext_drv->config->table_init_on  = &mipi_init_on_table[0];
	ext_drv->config->table_init_off = &mipi_init_off_table[0];

	return 0;
}

int aml_lcd_extern_mipi_p070acb_get_default_index(void)
{
	return LCD_EXTERN_INDEX;
}

int aml_lcd_extern_mipi_p070acb_probe(struct aml_lcd_extern_driver_s *ext_drv)
{
	int ret = 0;

	ret = lcd_extern_driver_update(ext_drv);

	if (lcd_debug_print_flag)
		EXTPR("%s: %d\n", __func__, ret);
	return ret;
}
