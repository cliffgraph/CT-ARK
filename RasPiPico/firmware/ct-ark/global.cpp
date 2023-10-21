#include "pico/stdlib.h"	// for uint8_t
#include "global.h"

// 動作モード
volatile SYSMODE g_SystemMode;

// メモリ管理
volatile MEM_SEGMENT g_Segments[MEM_SEGMENT_NUM];
volatile MEM_SEGMENT g_InvalidSeg;
volatile uint8_t *g_pMemSts[MSX_PAGE_NUM];
volatile bool g_bMarked;




