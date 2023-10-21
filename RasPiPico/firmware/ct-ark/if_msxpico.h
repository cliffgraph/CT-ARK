#pragma once
#include <stdint.h>

#pragma pack(push,1)

enum class IFCMD : uint8_t
{
	NONE			= 0x00,	// none
	REQ_FILELIST	= 0x01,	// Request file list
	SET_ROM			= 0x02,	// Set number of boot rom file, 0:No Select, 1-54:Selected No
	DEL_ROMFILE		= 0x03,	// Delete file, 1-54:Selected No
	WR_FILE			= 0x04,	// ROM->FILE, blockno, name strings, block data
	WRT_FLASHROM	= 0x05,	// FILE->FlushROM, blockno, name strings, block data
	BAS2ROM			= 0x06,	// BASIC Program file -> ROM File, Selected No
};

struct MSXPICOIF_FILERECORD
{
	uint8_t		mark;
	uint8_t		name[8+1+3+1];	// name	  ex) "GAME.ROM" 終端は\0
};

struct MSXPICOIF_A
{
	uint8_t	magic_word[12];			// msx -> pico	"OPEN THE ARK"
	uint8_t	pico_side_version[5+1];	// msx <- pico	"xx.xx" + \0
	uint8_t	pico_clock_mhz[5+1];	// msx <- pico	"(xxx)" Mhz
	uint8_t	boot_rom_name[8+1+3+1];	// msx <- pico	"xxxxxxxx.xxx" + \0
	IFCMD	cmd;					// msx -> pico
	uint8_t	cmd_value;				// msx -> pico
	uint8_t	cmd_fwr_name[8+1];		// msx -> pico
	IFCMD	cmd_res;				// msx <- pico
};

struct MSXPICOIF_B
{
	uint8_t					status;			// 0:no sd card, !0:files
	uint8_t					num_files;
	MSXPICOIF_FILERECORD	files[3*18];
};

struct MSXPICOIF_C
{
	uint8_t		block_data[0x2000];
};

union MSXPICOIF_BC
{
	MSXPICOIF_B	B;	// msx <- pico
	MSXPICOIF_C	C;	// msx -> pico
};

#pragma pack(pop)


