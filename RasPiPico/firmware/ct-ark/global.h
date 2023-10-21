#pragma once


// メモリ管理
const int MEM_SEGMENT_SIZE = 16*1024;
const int MEM_SEGMENT_NUM = 12;
struct MEM_SEGMENT
{
	uint8_t mem[MEM_SEGMENT_SIZE];
};
extern volatile MEM_SEGMENT g_Segments[MEM_SEGMENT_NUM];
extern volatile MEM_SEGMENT g_InvalidSeg;

const int MSX_PAGE_NUM = 4;
extern volatile uint8_t *g_pMemSts[MSX_PAGE_NUM];

// 動作モード
enum class SYSMODE
{
	NONE,
	ROM32K,		// RAM上
	RAM,
	CTARKMENU,
	FlashROM,	// FlashROM上
};
extern volatile SYSMODE g_SystemMode;
extern volatile bool g_bMarked;

const uint32_t FLASH_ROM_TOP_ADDRESS = 0x1F0000;
