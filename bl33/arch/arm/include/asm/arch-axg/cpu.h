
/*
 * arch/arm/include/asm/arch-axg/cpu.h
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef _CPU_H
#define _CPU_H
//#include <config.h>
//#include <asm/plat-cpu.h>
//#include <asm/arch/ddr.h>

#define CONFIG_AML_MESON 1
#define CONFIG_AML_MESON_GX 1
#define CONFIG_AML_MESON_AXG 1

/* Size of malloc() pool */
#define CONFIG_SYS_MALLOC_LEN		(15*1024*1024) /* 128M ddr support max 11M malloc */

/* dram */
#define PHYS_SDRAM_1_BASE					0x00000000UL
#define PHYS_SDRAM_1_SIZE					(CONFIG_DDR_SIZE << 20)
#define CONFIG_SYS_SDRAM_BASE				PHYS_SDRAM_1_BASE
#define CONFIG_SYS_INIT_SP_ADDR				(0x08000000)

#define CONFIG_SYS_TEXT_BASE		0x01000000 /*16MB rsv*/
#define CONFIG_SYS_LOAD_ADDR		(PHYS_SDRAM_1_BASE + CONFIG_SYS_TEXT_BASE)

/* ? */
#define CONFIG_SYS_CBSIZE		512
#define CONFIG_SYS_PROMPT		SYS_PROMPT
#define CONFIG_SYS_PBSIZE		(CONFIG_SYS_CBSIZE + sizeof(CONFIG_SYS_PROMPT) + 16)

/* Generic Timer Definitions */
#define COUNTER_FREQUENCY		(0x1800000)	/* 24MHz */

/* support early init */
#define CONFIG_BOARD_EARLY_INIT_F
/* support board late init */
#define CONFIG_BOARD_LATE_INIT
/* use "hush" command parser */
#define CONFIG_SYS_HUSH_PARSER

#define CONFIG_DTB_MEM_ADDR 0x1000000

#define CONFIG_AML_CUSTOMER_ID  (0)

//#define CONFIG_SILENT_CONSOLE
#define CONFIG_SILENT_CONSOLE_UPDATE_ON_RELOC
#define CONFIG_SILENT_U_BOOT_ONLY

//2017.04.26 temp disable compress for txlx for BL31 not ready
#define CONFIG_AML_BL30_COMPRESS_DISABLE 1
#define CONFIG_AML_BL32_COMPRESS_DISABLE 1
#define CONFIG_AML_BL33_COMPRESS_DISABLE 1


//2017.05.19 AXG only support v3 format for normal & secure boot
#define CONFIG_AML_SECURE_BOOT_V3 1


//2017.06.08 new compress solution, only support BL33 LZ4 compress
#define CONFIG_AML_BL33_COMPRESS_ENABLE 1

#endif /* _CPU_H */
