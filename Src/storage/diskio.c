/*
 * diskio.c
 *
 *  Created on: Jun 30, 2026
 *      Author: brettsodie
 */

#include <stdio.h>
#include "ff.h"
#include "diskio.h"
#include "sdcard.h"

volatile int dbgWatch = 0;   // put this above disk_read, outside the function

DSTATUS disk_initialize(BYTE pdrv) {
    printf("disk_initialize called\r\n");
    if (sdInit()) {
        printf("sdInit succeeded\r\n");
        return 0;
    }
    printf("sdInit failed\r\n");
    return STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {

	// then at the top of disk_read:
	if (dbgWatch) {
	    printf("disk_read: %u sectors\r\n", count);
	}

    return sdReadBlocks(sector, buff, count) ? RES_OK : RES_ERROR;
}

//DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
////	printf("disk_read called: sector %lu count %u\r\n", sector, count);
//    for (UINT i = 0; i < count; i++) {
//        if (!sdReadBlock(sector + i, buff + (i * 512))) {
//            return RES_ERROR;
//        }
//    }
//    return RES_OK;
//}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    for (UINT i = 0; i < count; i++) {
        if (!sdWriteBlock(sector + i, buff + (i * 512))) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

DWORD get_fattime(void) {
    return 0;
}
