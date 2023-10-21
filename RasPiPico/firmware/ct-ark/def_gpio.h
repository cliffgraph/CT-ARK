/**
 * CT-ARK (RaspberryPiPico firmware)
 * Copyright (c) 2023 Harumakkin.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hardware/spi.h"

const uint32_t GP_D0_A0						= 0;
const uint32_t GP_D1_A1						= 1;
const uint32_t GP_D2_A2						= 2;
const uint32_t GP_D3_A3						= 3;
const uint32_t GP_D4_A4						= 4;
const uint32_t GP_D5_A5						= 5;
const uint32_t GP_D6_A6						= 6;
const uint32_t GP_D7_A7						= 7;
//
const uint32_t GP_SIRW_A8					= 8;
const uint32_t GP_RD_A9						= 9;
const uint32_t GP_WR_A10					= 10;
const uint32_t GP_SLTSL_A11					= 11;
const uint32_t GP_IOREQ_A12					= 12;
const uint32_t GP_MERQ_A13					= 13;
const uint32_t GP_CS12_A14					= 14;
const uint32_t GP_SLIO_A15					= 15;
//
const uint32_t GP_AD0_AD8_G					= 20;
const uint32_t GP_C_D0_G					= 21;
const uint32_t GP_D0_DIR					= 22;
const uint32_t GP_J2						= 26;
const uint32_t GP_MODE_SW					= 27;	// pulluped, H=Cartridge mode, L=Reader Mode
const uint32_t GP_J1						= 28;
//
const uint32_t GP_PICO_LED					= 25;	// Pico's LED.


// SPI
#define SPIDEV spi0
#define DEF_SPI_TX_PIN  19		// GP19 spi0 TX	 pin.25
#define DEF_SPI_RX_PIN  16		// GP16 spi0 RX	 pin.21
#define DEF_SPI_SCK_PIN 18		// GP18 spi0 SCK pin.24
#define DEF_SPI_CSN_PIN 17		// GP17 spi0 CSn pin.22



