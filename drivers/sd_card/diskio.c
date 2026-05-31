
#include "ff.h"
#include "diskio.h"
#include "sd_spi.h"
#include "ti_msp_dl_config.h"

#define DEV_MMC  0

static volatile DSTATUS g_stat = STA_NOINIT;

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != DEV_MMC) {
        return STA_NOINIT;
    }

    return g_stat;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != DEV_MMC) {
        return STA_NOINIT;
    }

    g_stat = STA_NOINIT;

    /* Give the card a little time after reset/power-up */
    delay_cycles(3200000);   /* ~100 ms at 32 MHz */

    if (SD_init()) {
        g_stat = 0;
        return g_stat;
    }

    /* Retry once more */
    delay_cycles(3200000);   /* ~100 ms */
    if (SD_init()) {
        g_stat = 0;
        return g_stat;
    }

    g_stat = STA_NOINIT;
    return g_stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    UINT i;

    if (pdrv != DEV_MMC) {
        return RES_PARERR;
    }

    if (g_stat & STA_NOINIT) {
        return RES_NOTRDY;
    }

    if ((buff == 0) || (count == 0)) {
        return RES_PARERR;
    }

    for (i = 0; i < count; i++) {
        if (SD_readBlock((uint32_t)(sector + i), &buff[i * 512U]) != SD_RES_OK) {
            return RES_ERROR;
        }
    }

    return RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    UINT i;

    if (pdrv != DEV_MMC) {
        return RES_PARERR;
    }

    if (g_stat & STA_NOINIT) {
        return RES_NOTRDY;
    }

    if ((buff == 0) || (count == 0)) {
        return RES_PARERR;
    }

    for (i = 0; i < count; i++) {
        if (SD_writeBlock((uint32_t)(sector + i), &buff[i * 512U]) != SD_RES_OK) {
            return RES_ERROR;
        }
    }

    return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    uint32_t value32;

    if (pdrv != DEV_MMC) {
        return RES_PARERR;
    }

    if (g_stat & STA_NOINIT) {
        return RES_NOTRDY;
    }

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;

    case GET_SECTOR_SIZE:
        if (buff == 0) {
            return RES_PARERR;
        }
        *(WORD *)buff = 512;
        return RES_OK;

    case GET_BLOCK_SIZE:
        if (buff == 0) {
            return RES_PARERR;
        }
        /*
         * Erase block size in units of sectors.
         * 8 sectors = 4096 bytes, a common safe value.
         */
        *(DWORD *)buff = 8;
        return RES_OK;

    case GET_SECTOR_COUNT:
        if (buff == 0) {
            return RES_PARERR;
        }

        if (SD_getSectorCount(&value32) != SD_RES_OK) {
            return RES_ERROR;
        }

        *(LBA_t *)buff = (LBA_t)value32;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}