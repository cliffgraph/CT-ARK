/**
 * CT-ARK (RaspberryPiPico firmware)
 * Copyright (c) 2023 Harumakkin.
 * SPDX-License-Identifier: MIT
 */
// https://spdx.org/licenses/


//#define FOR_DEGUG

//char __flash_binary_end;

#include <stdio.h>
#include <memory.h>
#include <stdint.h>
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <hardware/flash.h>
#ifdef FOR_DEGUG
#include <hardware/clocks.h>
#endif
#include "ff.h"
#include "diskio.h"
#include "ct-ark.pio.h"
#include "def_gpio.h"
#include "global.h"
#include "title.h"
#include "sdfat.h"
#include "if_msxpico.h"

#ifdef FOR_DEGUG
volatile uint32_t Wffffcnt = 0;
volatile uint32_t Wffffcnt_bak = 0;
volatile uint32_t Wffff = 123;
#endif


struct INITGPTABLE {
	int gpno;
	int direction;
	bool bPullup;
	int	init_value;
};

// カートリッジ動作モード
static const INITGPTABLE g_CartridgeMode_GpioTable[] = {
	{ GP_D0_DIR,		GPIO_OUT,		false, 0, },	// 0=MSX->PICO	(必ずプルアップはfalseとする）
	{ GP_AD0_AD8_G,		GPIO_OUT,		false, 1, },	// 0=通、1=不通	(必ずプルアップはfalse、且つ不通とする）
	{ GP_C_D0_G,		GPIO_OUT,		false, 1, },	// 0=通、1=不通	(必ずプルアップはfalse、且つ不通とする）
//
	{ GP_J1,			GPIO_OUT,		false, 1, },
	{ GP_J2,			GPIO_OUT,		false, 0, },
	{ GP_PICO_LED,		GPIO_OUT,		false, 0, },
	{ GP_MODE_SW,		GPIO_IN,		true,  0, },	// (プルアップは必ずtrueとする）
//
	{ GP_D0_A0,			GPIO_IN,		false, 0, },
	{ GP_D1_A1,			GPIO_IN,		false, 0, },
	{ GP_D2_A2,			GPIO_IN,		false, 0, },
	{ GP_D3_A3,			GPIO_IN,		false, 0, },
	{ GP_D4_A4,			GPIO_IN,		false, 0, },
	{ GP_D5_A5,			GPIO_IN,		false, 0, },
	{ GP_D6_A6,			GPIO_IN,		false, 0, },
	{ GP_D7_A7,			GPIO_IN,		false, 0, },
	{ GP_SIRW_A8,		GPIO_IN,		false, 0, },
	{ GP_RD_A9,			GPIO_IN,		false, 0, },
	{ GP_WR_A10,		GPIO_IN,		false, 0, },
	{ GP_SLTSL_A11,		GPIO_IN,		false, 0, },
	{ GP_IOREQ_A12,		GPIO_IN,		false, 0, },
	{ GP_MERQ_A13,		GPIO_IN,		false, 0, },
	{ GP_CS12_A14,		GPIO_IN,		false, 0, },
	{ GP_SLIO_A15,		GPIO_IN,		true, 0, },
	{ -1,						0,		false, 0, },	// eot
};

const uint8_t *g_pFlashRom = (const uint8_t *)(XIP_BASE + FLASH_ROM_TOP_ADDRESS);
//const uint8_t *g_pFlashRom = (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + FLASH_ROM_TOP_ADDRESS);


static char g_RomName[8+1+3+1];

static void clearRomName()
{
	strcpy(g_RomName, "...         ");		// 12-length
	return;
}

static const char *pSDWORKDIR = "\\ct-ark";
static char workPath[255+1];
static void makeDirWork()
{
	sd_fatMakdir(pSDWORKDIR);
	return;
}

static void makeUpFileList(volatile MSXPICOIF_B *pDest)
{
	for( int t = 0; t < 3*18; ++t) {
		memcpy((void*)&pDest->files[t], " ...         \x00", 14);
	}

	static FATFS g_fs;
    FRESULT ret = f_mount( &g_fs, "", 1 );
    if( ret != FR_OK ) {
		pDest->status = 0x00;	// no sd card
        return;
    }
	DIR dirObj;
	FILINFO fno;
	FRESULT fr;
	uint8_t count = 0;

	fr = f_findfirst(&dirObj, &fno, pSDWORKDIR, "*.ROM");
    while (fr == FR_OK && fno.fname[0]) {
		auto &item = pDest->files[count++];
		item.mark = ' ';
		strcpy((char*)item.name, fno.fname);
        fr = f_findnext(&dirObj, &fno); 
    }
	f_closedir(&dirObj);

	fr = f_findfirst(&dirObj, &fno, pSDWORKDIR, "*.BAS");
    while (fr == FR_OK && fno.fname[0]) {
		auto &item = pDest->files[count++];
		item.mark = ' ';
		strcpy((char*)item.name, fno.fname);
        fr = f_findnext(&dirObj, &fno);
    }
	f_closedir(&dirObj);
	pDest->num_files = count;

	//
	pDest->status = 0x01;
	return;
}

static void setupGpio(const INITGPTABLE pTable[] )
{
	for (int t = 0; pTable[t].gpno != -1; ++t) {
		const int no = pTable[t].gpno;
		gpio_init(no);
		//gpio_set_irq_enabled(no, 0xf, false);
		gpio_set_dir(no, pTable[t].direction);
		gpio_put(no, pTable[t].init_value);
		if (pTable[t].bPullup)
		 	gpio_pull_up(no);
		else
		 	gpio_disable_pulls(no);
	}
	return;
}

// ファイル名に使用できる文字かどうかチェックする
bool checkFilespecChar(const uint8_t ch)
{
	if(('0' <= ch && ch <= '9') )
		return true;
	if(('a' <= ch && ch <= 'z') )
		return true;
	if(('A' <= ch && ch <= 'Z') )
		return true;
	const char *pChars = "$&#@!%'()-{}~_";
	for( int t = 0; pChars[t] != '\0'; ++t ) {
		if( pChars[t] == (char)ch )
			return true;
	}
	return false;
}

inline void setMemMap(int pageIndex, int segIndex)
{
	g_pMemSts[pageIndex] =
		(segIndex < MEM_SEGMENT_NUM)
		? g_Segments[segIndex].mem
		: g_InvalidSeg.mem;
	return;
}

static const char *g_pPATH_BOOTNAME = "bootname";
static bool saveBootNameFile(const char *pNameArea, const size_t sz)
{
	makeDirWork();
	sprintf(workPath, "%s\\%s", pSDWORKDIR, g_pPATH_BOOTNAME);
	sd_fatWriteFileTo(workPath, pNameArea, (int)sz );
	return true;
}

static bool loadBootNameFile(const char *pNameArea)
{
	bool bGood = false;
	UINT readSize = 0;
	static const UINT SIZEF = 8+1+3+1;
	sprintf(workPath, "%s\\%s", pSDWORKDIR, g_pPATH_BOOTNAME);
	if(sd_fatReadFileFrom(workPath, SIZEF, (uint8_t*)pNameArea, &readSize) ) {
		bGood = (0 <readSize && readSize <= SIZEF) ? true : false;
	}
	return bGood;
}

static bool loadRomImage(const char *pFname)
{
	bool bGood = false;
	UINT readSize = 0;
	static const UINT SIZE_ROM32K = 32*1024;

	sprintf(workPath, "%s\\%s", pSDWORKDIR, pFname);
	if(sd_fatReadFileFrom(workPath, SIZE_ROM32K, (uint8_t*)g_pMemSts[1]/*page.1*/, &readSize) ) {
		if( readSize <= SIZE_ROM32K ) {
			bGood = true;
		}
	}
	return bGood;
}

static void taskType_RAM(
	PIO pio, uint adCartridge, uint smCartridge, uint32_t RETCMD)
{
 	for( int t = 0; t < MSX_PAGE_NUM; t++){
 		setMemMap(t, MSX_PAGE_NUM-t-1);
 		//setMemMap(t, t);
 	}
 	for( int t = 0; t < MSX_PAGE_NUM; t++){
		g_Segments[t].mem[0x20] = 'S';
		g_Segments[t].mem[0x21] = '0'+t;
 	}

	// msxCtrl = [SLIO][CS12][MERQ][IOREQ],[SLTSL][WR][RD][SIRW]

	for(;;){
		const uint32_t wgBus = pio_sm_get_blocking(pio, smCartridge);
		const uint8_t msxCtrl = static_cast<uint8_t>((wgBus>>24) ^ 0xff) & 0x1F;
		// SLSTS & RD（メモリリード）
		if( msxCtrl == 0x0B ) {
			const uint16_t msxAddr = wgBus & 0xffff;
			const int pageNo = msxAddr >> 14;
			auto *pMem = g_pMemSts[pageNo];
			const uint16_t offset = msxAddr & 0x3fff;
			const uint32_t dt32 = (pMem[offset]<<8)| 0xff;
			pio_sm_put_blocking(pio, smCartridge, dt32);
		}
		// SLTSL & WR（!RD=メモリライト）	// この時点ではまだWR=0
		else if( msxCtrl == 0x0D ) {
			const uint16_t msxAddr = wgBus & 0xffff;
			const int pageNo = msxAddr >> 14;
			auto *pMem = g_pMemSts[pageNo];
			if( pMem != g_InvalidSeg.mem ){
				const uint8_t dt8 = (wgBus >> 16) & 0xff;
				const uint16_t offset = msxAddr & 0x3fff;
				pMem[offset] = dt8;
			}
		}
		//IOREQ & WR（I/Oライト）
		else if( msxCtrl == 0x15 ) {
			const int pg = (int)(wgBus & 0xff) - 0xfc;
			const uint8_t dt8 = (wgBus >> 16) & 0xff;
			if( 0 <= pg ){
				setMemMap(pg, dt8);
			}
		}
		else {
			pio_sm_exec(pio, smCartridge, RETCMD);
		}
	}
	return;
}

static void taskType_32kROM(
	PIO pio, uint adCartridge, uint smCartridge, uint32_t RETCMD)
{
	auto *pMem = g_pMemSts[0];

	// msxCtrl = [SLIO][CS12][MERQ][IOREQ],[SLTSL][WR][RD][SIRW]

	// ROMカートリッジ
	for(;;){
		uint32_t wgBus = pio_sm_get_blocking(pio, smCartridge);
		uint8_t msxCtrl = static_cast<uint8_t>((wgBus>>24) ^ 0xff) & 0x1F;
		// SLSTS & RD（メモリリード）
		if( msxCtrl == 0x0B) {
			uint16_t msxAddr = wgBus & 0xffff;
			uint32_t dt32 = (pMem[msxAddr] << 8) | 0xff;
			pio_sm_put_blocking(pio, smCartridge, dt32);
		}
		// SLTSL & WR（!RD=メモリライト）
		else if( msxCtrl == 0x0D ) {
			// do nothing
		}
		// IOREQ & WR
		else if( msxCtrl == 0x15) {
			// do nothing
		}
		else {
			pio_sm_exec(pio, smCartridge, RETCMD);
		}
	}
	return;
}

// CT-ARKメニューモード用
static const uint8_t g_pStrMagicWord[] = "OPEN THE ARK";
static const uint8_t g_pStrVer[] = "0.83\0";

#if SYS_CLK_KHZ == _u(125000)
static const uint8_t g_pStrClock[] = "(125)\0";
#elif SYS_CLK_KHZ == _u(170000)
static const uint8_t g_pStrClock[] = "(170)\0";
#elif SYS_CLK_KHZ == _u(240000)
static const uint8_t g_pStrClock[] = "(240)\0";
#else
static const uint8_t g_pStrClock[] = "(---)\0";
#endif

static void taskType_CTARKMENU(
	PIO pio, uint adCartridge, uint smCartridge,
	uint32_t RETCMD)
{

	auto  *pMem = g_pMemSts[0];

	const uint8_t *pStrArk = g_pStrMagicWord;
	uint16_t addrArk = 0x5F00;
	auto *pIf_A = reinterpret_cast<volatile MSXPICOIF_A *>(&pMem[0x5F00]);
	auto *pIf_BC = reinterpret_cast<volatile MSXPICOIF_BC *>(&pMem[0x6000]);

	pIf_A->cmd_res		= IFCMD::NONE;
	pIf_A->cmd			= IFCMD::NONE;
	pIf_BC->B.status	= 0;
	pIf_BC->B.num_files	= 0;
	

	g_bMarked = false;

	// msxCtrl = [SLIO][CS12][MERQ][IOREQ],[SLTSL][WR][RD][SIRW]

	for(;;){
		const uint32_t wgBus = pio_sm_get_blocking(pio, smCartridge);
		const uint8_t msxCtrl = static_cast<uint8_t>((wgBus>>24) ^ 0xff) & 0x1F;
		const uint16_t msxAddr = wgBus & 0xffff;
		// SLSTS & RD（メモリリード）
		if( msxCtrl == 0x0B ) {
			const uint32_t dt32 = (pMem[msxAddr] << 8) | 0xff;
			pio_sm_put_blocking(pio, smCartridge, dt32);
		}
		// SLTSL & WR（!RD=メモリライト）
		else if( msxCtrl == 0x0D ) {
			const uint8_t dt8 = (wgBus >> 16) & 0xff;
			if( g_bMarked ){
				pMem[msxAddr] = dt8;
			}
			// マジックワードの記入をチェックする
			else if( msxAddr == addrArk && *pStrArk == dt8 ) {
				++pStrArk, ++addrArk;
				if( *pStrArk == '\0' ){
					g_bMarked = true;
				}
			}
		}
		// IOREQ & WR
		else if( msxCtrl == 0x15) {
			// do nothing
		}
		else {
			pio_sm_exec(pio, smCartridge, RETCMD);
		}
	}
	return;
}

static void taskType_FlashROM(
	PIO pio, uint adCartridge, uint smCartridge, uint32_t RETCMD)
{
	auto *pMem = g_pFlashRom;

	// ROMカートリッジ
	for(;;){
		uint32_t wgBus = pio_sm_get_blocking(pio, smCartridge);
		uint8_t msxCtrl = static_cast<uint8_t>((wgBus>>24) ^ 0xff) & 0x1F;
		// SLSTS & RD（メモリリード）
		if( msxCtrl == 0x0B) {
			uint16_t msxAddr = wgBus & 0xffff;
			uint32_t dt32 = (pMem[msxAddr] << 8) | 0xff;
			pio_sm_put_blocking(pio, smCartridge, dt32);
		}
		// SLTSL & WR（!RD=メモリライト）
		else if( msxCtrl == 0x0D ) {
			// do nothing
		}
		// IOREQ & WR
		else if( msxCtrl == 0x15) {
			// do nothing
		}
		else {
			pio_sm_exec(pio, smCartridge, RETCMD);
		}
	}
	return;
}


static void taskType_FAIL()
{
	uint8_t b = 0;
	for(;;) {
		gpio_put(GP_PICO_LED, b^=1);
		sleep_ms(300);
	}
	return;
}

static void Core1Task()
{
	const uint16_t RETCMD = 0x0400;

	// PIO の開始
 	PIO pio = pio0;
	uint ad;
	uint sm;
	if( SYS_CLK_KHZ == _u(170000)) {
		ad	= pio_add_program( pio, &pio_cartridge_170_program );
		sm = pio_claim_unused_sm( pio, true );
		pio_cartridge_170_program_init(
			pio, sm, ad,
			GP_D0_A0, GP_SLIO_A15 - GP_D0_A0+1,
			GP_RD_A9, GP_AD0_AD8_G,  GP_D0_DIR-GP_AD0_AD8_G+1 );
	}
	else if( SYS_CLK_KHZ == _u(240000)) {
		ad	= pio_add_program( pio, &pio_cartridge_240_program );
		sm = pio_claim_unused_sm( pio, true );
		pio_cartridge_240_program_init(
			pio, sm, ad,
			GP_D0_A0, GP_SLIO_A15 - GP_D0_A0+1,
			GP_RD_A9, GP_AD0_AD8_G,  GP_D0_DIR-GP_AD0_AD8_G+1 );
	}
	else {
		ad	= pio_add_program( pio, &pio_cartridge_125_program );
		sm = pio_claim_unused_sm( pio, true );
		pio_cartridge_125_program_init(
			pio, sm, ad,
			GP_D0_A0, GP_SLIO_A15 - GP_D0_A0+1,
			GP_RD_A9, GP_AD0_AD8_G,  GP_D0_DIR-GP_AD0_AD8_G+1 );
	}

	// メモリアクセス・メインループ
	switch(g_SystemMode)
	{
		case SYSMODE::RAM:
			taskType_RAM(pio, ad, sm, RETCMD);
			break;
		case SYSMODE::ROM32K:
			taskType_32kROM(pio, ad, sm, RETCMD);
			break;
		case SYSMODE::CTARKMENU:
			taskType_CTARKMENU(pio, ad, sm, RETCMD);
			break;
		case SYSMODE::FlashROM:
			taskType_FlashROM(pio, ad, sm, RETCMD);
			break;
		case SYSMODE::NONE:
		default:
			taskType_FAIL();
			break;
	}
	return;
}

static uint16_t t_GetPtrByLowNo(volatile uint8_t *p, const int tgtNo)
{
	int nextPtr;
	int index = 0;
	index++;
	for(;;) {
		nextPtr = (p[index+1]<<8)|p[index+0];
		if( nextPtr == 0 )
			break;
		const int no = (p[index+3]<<8)|p[index+2];
		if( no == tgtNo ){
			break;
		}
		index = nextPtr - 0x8000;
	}
	return nextPtr;
}

// BASICプログラム(ファイル）をROMファイル化する。
// @note 先頭アドレスは0x8001で作成されたBASICプログラム前提とする
//       BASICプログラム中のアドレス値を、romヘッダの16bytes分後ろへ移動させる(+16する)
static bool t_ReassignmentAddress(volatile uint8_t *p)
{
	volatile uint8_t *pSrc = p;
	const uint16_t DIFFADDR = 16;
	uint16_t temp16;
	if( *p != 0xff )
		return false;
	*p = 0x00;
	p++;
	for(;;) {
		// 次行を示すリンクポインタ（0なら修正終了)
		// auto *pTemp16 = reinterpret_cast<volatile uint16_t*>(p);	// ←なぜかハングアップする
		temp16 = (p[1]<<8)|p[0];
		if( temp16 == 0x0000 )
			break;
		temp16 += DIFFADDR;
		p[0] = temp16 & 0xff;
		p[1] = temp16 >> 8;
		p+=2;
		// 行番号
		p+=2;	// 読み捨て
		// (マルチ)ステートメント内の修正
		bool bStr = false;
		bool bRem = false;
		for(;;) {
			//printf( "S %02x\n", *p );
			if( *p == 0x00 ){	// 終端なら修正終了
				p++;
				break;
			}
			if( bRem ) {		// 終端まで読み捨て
				p++;
			}
			else if( bStr ){
				if( *p == '\"' )
					bStr = false;
				p++;
			}
			else {
				switch(*p++)
				{
					case '\"':	bStr = true; break;
					case 0x01:	p+=1;	break;	// グラフィック文字
					case 0x0b:	p+=2;	break;	// 8進数
					case 0x0c:	p+=2;	break;	// 16進数
					case 0x0d:	// 行番号(絶対アドレス)
						// pTemp16 = reinterpret_cast<volatile uint16_t*>(p);
						temp16 = (p[1]<<8)|p[0];
						temp16 += DIFFADDR;
						p[0] = temp16 & 0xff;
						p[1] = temp16 >> 8;
						p+=2;
						break;
					case 0x0e:	// 行番号(行番号)は、
					{
						uint16_t tgtNo = (p[1]<<8)|p[0];
						temp16 = t_GetPtrByLowNo(pSrc, tgtNo);
						if( temp16 != 0x0000 ){
							temp16 += DIFFADDR;
							p[-1] = 0x0d;
							p[0] = temp16 >> 8;
							p[1] = temp16 & 0xff;
						}
						p+=2;
						break;
					}
					case 0x0f:	p+=1;	break;	// 整数 10～255
					case 0x1c:	p+=2;	break;	// 整数 256～32767
					case 0x1d:	p+=4;	break;	// 単精度実数
					case 0x1f:	p+=8;	break;	// 倍精度実数
					case 0x26:					// &B二進数
						if( *p == 'B' ){
							p++;
							while(*p=='0'||*p=='1') p++;
						}
						break;
					case 0x3a:
						if( *p == 0xa1 ){		// ELSE
							p++;
							break;
						}
						if( *p == 0x8f ){
							p++;
							if( *p == 0xe6 ) {	// '
								bRem = true;
								p++;
							}
						}
						break;
					case 0x8f:	// REM
						bRem = true;
						break;
					case 0xff:
						if( (*p == 0x3e) ||
							(0x81 <= *p && *p <= 0x8d) || 
							(0x8f <= *p && *p <= 0xAf) || 
							(*p == 0xb0) ) {
								p++;
						}
						break;
					default:
						break;
				}
			}
		}
	}
	return true;
}

static void Core0Task()
{
	bool bMarkedOld = g_bMarked;

	if( g_SystemMode != SYSMODE::CTARKMENU ) {
#ifdef FOR_DEGUG
		for(;;) {
			if( Wffffcnt_bak != Wffffcnt ){
				uint32_t kj = Wffff;
				Wffffcnt_bak = Wffffcnt;
				printf( "W %04x %04x\n", kj>>16, kj&0xffff );
			}
		}
#else
		// core0 は停止します
		multicore_lockout_victim_init();
#endif
	}
	else {
		volatile uint8_t *pMem = g_pMemSts[0];
		auto *pIf_A = reinterpret_cast<volatile MSXPICOIF_A *>(&pMem[0x5F00]);
		auto *pIf_BC = reinterpret_cast<volatile MSXPICOIF_BC *>(&pMem[0x6000]);

		for(;;) {
			if( bMarkedOld != g_bMarked ){
				bMarkedOld = g_bMarked;
				memcpy((void *)pIf_A->magic_word, g_pStrMagicWord, sizeof(g_pStrMagicWord)-1);
				memcpy((void *)pIf_A->pico_side_version, g_pStrVer, sizeof(g_pStrVer));
				memcpy((void *)pIf_A->pico_clock_mhz, g_pStrClock, sizeof(g_pStrClock));
			}
			if( !g_bMarked || pIf_A->cmd_res != IFCMD::NONE )
				continue;
			const IFCMD cmd = pIf_A->cmd;
			switch( cmd )
			{
				case IFCMD::REQ_FILELIST:	// ファイルリストを要求されている
				{
					makeUpFileList(&(pIf_BC->B));
					strcpy((char*)pIf_A->boot_rom_name, g_RomName);
					pIf_A->cmd = IFCMD::NONE;
					pIf_A->cmd_res = cmd;
					break;
				}
				case IFCMD::SET_ROM:
				{
					int no = pIf_A->cmd_value;
					if( no == 0 ){
						clearRomName();
						saveBootNameFile(g_RomName, sizeof(g_RomName));
					}
					else if( no <= (int)pIf_BC->B.num_files && pIf_BC->B.status != 0) {
						strcpy(g_RomName, (char*)pIf_BC->B.files[no-1].name);
						saveBootNameFile(g_RomName, sizeof(g_RomName));
					}
					strcpy((char*)pIf_A->boot_rom_name, g_RomName);
					pIf_A->cmd = IFCMD::NONE;
					pIf_A->cmd_res = cmd;
					break;
				}
				case IFCMD::DEL_ROMFILE:
				{
					int no = pIf_A->cmd_value;
					if( 0 < no && no <= (int)pIf_BC->B.num_files && pIf_BC->B.status != 0) {
						char *p = (char*)pIf_BC->B.files[no-1].name;
						if( strcmp(p, g_RomName) == 0 ) {
							clearRomName();
						}
						sd_faRemoveFile(p);
						makeUpFileList(&(pIf_BC->B));
						strcpy((char*)pIf_A->boot_rom_name, g_RomName);
					}
					pIf_A->cmd = IFCMD::NONE;
					pIf_A->cmd_res = cmd;
					break;
				}
				case IFCMD::WR_FILE:
				{
					int cnt = 0;
					char name[8+1];
					for(int t = 0; t < 8; ++t) {
						uint8_t ch = pIf_A->cmd_fwr_name[t];
						if( ch == ' ' || ch == '\x0' )
							break;
						if( checkFilespecChar(ch) ) {
							name[cnt++] = ch;
						}
					}
					name[cnt] = '\0';
					if( 0 < cnt ) {
						sprintf(workPath, "%s\\%s.ROM", pSDWORKDIR, name);
						int sectNo = pIf_A->cmd_value;
						auto *pBuff = (char *)&pIf_BC->C.block_data;
						makeDirWork();
						sd_fatWriteFileTo(workPath, pBuff, 0x2000, (sectNo!=0) );
					}
					pIf_A->cmd = IFCMD::NONE;
					pIf_A->cmd_res = cmd;
					break;
				}
				case IFCMD::WRT_FLASHROM:
				{
					int no = pIf_A->cmd_value;
					if( 0 < no && no <= (int)pIf_BC->B.num_files && pIf_BC->B.status != 0) {
						const char *pName = (char*)pIf_BC->B.files[no-1].name;
						sprintf(workPath, "%s\\%s", pSDWORKDIR, pName);
						volatile uint8_t *pDt = g_Segments[MEM_SEGMENT_NUM-2].mem;	// 一時領域 32KB
						const int buffSize = 16*1024*2;
						UINT readSize;
						if( sd_fatReadFileFrom(workPath, buffSize, (uint8_t*)pDt, &readSize) ) {
							const uint32_t ints = save_and_disable_interrupts();
							flash_range_erase(FLASH_ROM_TOP_ADDRESS+16*1024, 32*1024);
							flash_range_program(FLASH_ROM_TOP_ADDRESS+16*1024, (uint8_t*)pDt, buffSize);
							restore_interrupts(ints);
						}
					}
					pIf_A->cmd = IFCMD::NONE;
					pIf_A->cmd_res = cmd;
					break;
				}
				case IFCMD::BAS2ROM:
				{
					int cnt = 0;
					char name[8+1];
					for(int t = 0; t < 8; ++t) {
						uint8_t ch = pIf_A->cmd_fwr_name[t];
						if( ch == ' ' || ch == '\x0' )
							break;
						if( checkFilespecChar(ch) ) {
							name[cnt++] = ch;
						}
					}
					name[cnt] = '\0';
					if( 0 < cnt ) {
						int no = pIf_A->cmd_value;
						if( 0 < no && no <= (int)pIf_BC->B.num_files && pIf_BC->B.status != 0) {
							const char *pName = (char*)pIf_BC->B.files[no-1].name;
							sprintf(workPath, "%s\\%s", pSDWORKDIR, pName);
							volatile uint8_t *pRom = g_Segments[MEM_SEGMENT_NUM-2].mem;	// 一時領域 16KB
							volatile uint8_t *pBas = g_Segments[MEM_SEGMENT_NUM-1].mem;	// 一時領域 16KB
							const int sizeBas = 16*1024*1;
							UINT readSize;
							if( sd_fatReadFileFrom(pName, sizeBas, (uint8_t*)pBas, &readSize) ) {
								if( t_ReassignmentAddress(pBas) ) {
									static const uint8_t head[] = {
										'A', 'B',
										0x00,0x00, 0x00,0x00, 0x00,0x00,	// INIT, STATEMENT, DEVICE
										0x10,0x80,	// TEXT
										0x00,0x00,0x00,0x00,0x00,0x00,
									};
									pRom[0] = 0x00;
									sprintf(workPath, "%s\\%s.ROM", pSDWORKDIR, name);
									makeDirWork();
									sd_fatWriteFileTo(workPath, (const char*)pRom, MEM_SEGMENT_SIZE);
									sd_fatWriteFileTo(workPath, (const char*)head, sizeof(head), true/*append*/);
									sd_fatWriteFileTo(workPath, (const char*)pBas, readSize, true/*append*/);
								}
							}
						}
					}
					pIf_A->cmd = IFCMD::NONE;
					pIf_A->cmd_res = cmd;
					break;
				}

				default:
				{
					// do nothing
					break;
				}
			}
		}
	}
	return;
}

int main()
{
	setupGpio(g_CartridgeMode_GpioTable);
	sleep_ms(1);

	disk_initialize(0);
	clearRomName();

	g_bMarked = false;
	g_SystemMode = SYSMODE::RAM;

 	for( int t = 0; t < MSX_PAGE_NUM; t++){
 		setMemMap(t, t);
 	}
	// SW［■・］の場合、事前に選択したROMファイルを読み込んで実行する
	//	ROMファイル読み込めた	-> ROM32K
	//	読み込めなかった		-> RAM
	bool bModeSwPosLeft = gpio_get(GP_MODE_SW);
	if (bModeSwPosLeft) {
		if( loadBootNameFile(g_RomName) ) {
			if(loadRomImage(g_RomName)) {
				g_SystemMode = SYSMODE::ROM32K;
			}
			else {
				clearRomName();
			}
		}
		if( g_SystemMode != SYSMODE::ROM32K ) {
			// ROMファイルの読み込みができなかった場合は、
			// 0x8010 番地以降にその理由を書き込んでおく
			auto p = reinterpret_cast<volatile uint8_t*>(g_pMemSts[1]);
			memcpy((void*)&p[0x10], "not found file", 14);
		}
	}
	// SW［・■］の場合、CT-ARKメニューを実行するモード
	//	メニューを読み込めた	-> CTARKMENU
	//	読み込めなかった		-> FlashROM
	else {
		loadBootNameFile(g_RomName);
		if( loadRomImage("ct-ark.sys") ) {
			g_SystemMode = SYSMODE::CTARKMENU;
		}
		else {
			if( (g_pFlashRom[0x4000] == 'A' && g_pFlashRom[0x4001] == 'B') || 
				(g_pFlashRom[0x8000] == 'A' && g_pFlashRom[0x8001] == 'B') ) {
				g_SystemMode = SYSMODE::FlashROM;
			}
		}
	}
	// LEDの点灯
	if( g_SystemMode != SYSMODE::RAM ) {
		gpio_put(GP_PICO_LED, 1);
	}

	//
	multicore_launch_core1(Core1Task);
	Core0Task();

	return 0;
}
