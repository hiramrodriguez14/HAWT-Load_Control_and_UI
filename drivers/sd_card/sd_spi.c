#include "ti_msp_dl_config.h"
#include "sd_spi.h"
#include <stdint.h>
#include <stdbool.h>


#define SD_SPI_INST     SPI_0_INST
#define SD_CS_PORT      GPIO_GRP_0_PORT
#define SD_CS_PIN       GPIO_GRP_0_CS_PIN


#define SD_TYPE_UNKNOWN 0
#define SD_TYPE_V1_SDSC 1
#define SD_TYPE_V2_SDSC 2
#define SD_TYPE_V2_SDHC 3


#define CMD0    0
#define CMD8    8
#define CMD9    9
#define CMD16   16
#define CMD17   17
#define CMD24   24
#define CMD55   55
#define CMD58   58
#define ACMD41  41


#define CMD0_ARG    0x00000000UL
#define CMD8_ARG    0x000001AAUL
#define CMD55_ARG   0x00000000UL
#define CMD58_ARG   0x00000000UL
#define ACMD41_ARG  0x40000000UL


#define CMD0_CRC    0x94
#define CMD8_CRC    0x86
#define CMDX_CRC    0x00


#define SD_INIT_RETRY           100U
#define SD_READ_TOKEN_TIMEOUT   100000UL
#define SD_WRITE_BUSY_TIMEOUT   250000UL


static uint8_t g_cardType = SD_TYPE_UNKNOWN;


static uint8_t SD_spiTxRx(uint8_t tx)
{
   while (DL_SPI_isBusy(SD_SPI_INST))
       ;


   DL_SPI_transmitData8(SD_SPI_INST, tx);


   while (DL_SPI_isBusy(SD_SPI_INST))
       ;


   return (uint8_t)DL_SPI_receiveData8(SD_SPI_INST);
}


static void SD_setSpiSlow(void)
{
   DL_SPI_disable(SD_SPI_INST);
   DL_SPI_setBitRateSerialClockDivider(SD_SPI_INST, 39);   /* ~400kHz @ 32MHz */
   DL_SPI_enable(SD_SPI_INST);
}


static void SD_setSpiFast(void)
{
   DL_SPI_disable(SD_SPI_INST);
   DL_SPI_setBitRateSerialClockDivider(SD_SPI_INST, 1);    /* 8MHz @ 32MHz */
   DL_SPI_enable(SD_SPI_INST);
}


static void SD_select(void)
{
   SD_spiTxRx(0xFF);
   DL_GPIO_clearPins(SD_CS_PORT, SD_CS_PIN);
   SD_spiTxRx(0xFF);
}


static void SD_deselect(void)
{
   SD_spiTxRx(0xFF);
   DL_GPIO_setPins(SD_CS_PORT, SD_CS_PIN);
   SD_spiTxRx(0xFF);
}


static uint8_t SD_readR1(void)
{
   uint8_t i;
   uint8_t r = 0xFF;


   for (i = 0; i < 16; i++) {
       r = SD_spiTxRx(0xFF);
       if (r != 0xFF) {
           break;
       }
   }


   return r;
}


static void SD_readR3R7(uint8_t *res)
{
   res[0] = SD_readR1();
   if (res[0] > 1) {
       return;
   }


   res[1] = SD_spiTxRx(0xFF);
   res[2] = SD_spiTxRx(0xFF);
   res[3] = SD_spiTxRx(0xFF);
   res[4] = SD_spiTxRx(0xFF);
}


static void SD_sendCommandPacket(uint8_t cmd, uint32_t arg, uint8_t crc)
{
   SD_spiTxRx((uint8_t)(cmd | 0x40));
   SD_spiTxRx((uint8_t)(arg >> 24));
   SD_spiTxRx((uint8_t)(arg >> 16));
   SD_spiTxRx((uint8_t)(arg >> 8));
   SD_spiTxRx((uint8_t)(arg));
   SD_spiTxRx((uint8_t)(crc | 0x01));
}


static uint8_t SD_sendCommandR1(uint8_t cmd, uint32_t arg, uint8_t crc)
{
   SD_sendCommandPacket(cmd, arg, crc);
   return SD_readR1();
}


static uint8_t SD_sendACMD41(void)
{
   uint8_t r;
   uint32_t i = SD_INIT_RETRY;


   do {
       SD_select();
       SD_sendCommandPacket(CMD55, CMD55_ARG, CMDX_CRC);
       (void)SD_readR1();
       SD_deselect();


       SD_select();
       if (g_cardType == SD_TYPE_V1_SDSC) {
           SD_sendCommandPacket(ACMD41, 0x00000000UL, CMDX_CRC);
       } else {
           SD_sendCommandPacket(ACMD41, ACMD41_ARG, CMDX_CRC);
       }
       r = SD_readR1();
       SD_deselect();


       i--;
   } while ((r != 0x00) && (i > 0));


   return r;
}


static uint32_t SD_addrFromSector(uint32_t sector)
{
   if (g_cardType == SD_TYPE_V2_SDHC) {
       return sector;
   }
   return sector * 512UL;
}


uint8_t SD_getCardType(void)
{
   return g_cardType;
}


bool SD_init(void)
{
   uint8_t res[5] = {0};
   uint32_t i;


   g_cardType = SD_TYPE_UNKNOWN;


   DL_GPIO_setPins(SD_CS_PORT, SD_CS_PIN);
   SD_setSpiSlow();


   /* >=74 clocks with CS high */
   SD_deselect();
   for (i = 0; i < 10; i++) {
       SD_spiTxRx(0xFF);
   }


   /* CMD0 until idle */
   i = 0;
   do {
       SD_select();
       SD_sendCommandPacket(CMD0, CMD0_ARG, CMD0_CRC);
       res[0] = SD_readR1();
       SD_deselect();
       i++;
   } while ((res[0] != 0x01) && (i < 10U));


   if (res[0] != 0x01) {
       return false;
   }


   /* CMD8 */
   SD_select();
   SD_sendCommandPacket(CMD8, CMD8_ARG, CMD8_CRC);
   SD_readR3R7(res);
   SD_deselect();


   if (res[0] == 0x01) {
       if (res[3] != 0x01) {
           return false;
       }
       if (res[4] != 0xAA) {
           return false;
       }


       SD_select();
       SD_sendCommandPacket(CMD58, CMD58_ARG, CMDX_CRC);
       SD_readR3R7(res);
       SD_deselect();


       g_cardType = SD_TYPE_V2_SDSC;




       if (SD_sendACMD41() != 0x00) {
           return false;
       }


       SD_select();
       SD_sendCommandPacket(CMD58, CMD58_ARG, CMDX_CRC);
       SD_readR3R7(res);
       SD_deselect();


       if (!(res[1] & 0x80U)) {
           return false;
       }


       if (res[1] & 0x40U) {
           g_cardType = SD_TYPE_V2_SDHC;
       } else {
           g_cardType = SD_TYPE_V2_SDSC;
       }
   }
   else if (res[0] == 0x05) {
       g_cardType = SD_TYPE_V1_SDSC;


       if (SD_sendACMD41() != 0x00) {
           return false;
       }


       SD_select();
       if (SD_sendCommandR1(CMD16, 512, CMDX_CRC) != 0x00) {
           SD_deselect();
           return false;
       }
       SD_deselect();
   }
   else {
       return false;
   }


   SD_setSpiFast();
   SD_deselect();
   return true;
}


sd_result_t SD_readBlock(uint32_t sector, uint8_t *buf)
{
   uint32_t addr = SD_addrFromSector(sector);
   uint32_t i;
   uint8_t r;
   uint8_t token = 0xFF;


   SD_select();


   r = SD_sendCommandR1(CMD17, addr, CMDX_CRC);
   if (r != 0x00) {
       SD_deselect();
       return SD_RES_ERROR;
   }


   for (i = 0; i < SD_READ_TOKEN_TIMEOUT; i++) {
       token = SD_spiTxRx(0xFF);
       if (token != 0xFF) {
           break;
       }
   }


   if (token != 0xFE) {
       SD_deselect();
       return SD_RES_BAD_TOKEN;
   }


   for (i = 0; i < 512U; i++) {
       buf[i] = SD_spiTxRx(0xFF);
   }


   SD_spiTxRx(0xFF);
   SD_spiTxRx(0xFF);


   SD_deselect();
   return SD_RES_OK;
}


sd_result_t SD_writeBlock(uint32_t sector, const uint8_t *buf)
{
   uint32_t addr = SD_addrFromSector(sector);
   uint32_t i;
   uint8_t r;
   uint8_t dataResp;


   SD_select();


   r = SD_sendCommandR1(CMD24, addr, CMDX_CRC);
   if (r != 0x00) {
       SD_deselect();
       return SD_RES_ERROR;
   }


   SD_spiTxRx(0xFE);


   for (i = 0; i < 512U; i++) {
       SD_spiTxRx(buf[i]);
   }


   SD_spiTxRx(0xFF);
   SD_spiTxRx(0xFF);


   dataResp = SD_spiTxRx(0xFF);
   if ((dataResp & 0x1FU) != 0x05U) {
       SD_deselect();
       return SD_RES_ERROR;
   }


   i = 0;
   while (SD_spiTxRx(0xFF) == 0x00U) {
       i++;
       if (i > SD_WRITE_BUSY_TIMEOUT) {
           SD_deselect();
           return SD_RES_TIMEOUT;
       }
   }


   SD_deselect();
   return SD_RES_OK;
}


sd_result_t SD_getSectorCount(uint32_t *count)
{
   uint8_t csd[16];
   uint8_t r1;
   uint8_t token = 0xFF;
   uint32_t i;


   if (count == 0) {
       return SD_RES_ERROR;
   }


   SD_select();


   r1 = SD_sendCommandR1(CMD9, 0, CMDX_CRC);
   if (r1 != 0x00) {
       SD_deselect();
       return SD_RES_ERROR;
   }


   for (i = 0; i < SD_READ_TOKEN_TIMEOUT; i++) {
       token = SD_spiTxRx(0xFF);
       if (token != 0xFF) {
           break;
       }
   }


   if (token != 0xFE) {
       SD_deselect();
       return SD_RES_BAD_TOKEN;
   }


   for (i = 0; i < 16U; i++) {
       csd[i] = SD_spiTxRx(0xFF);
   }


   SD_spiTxRx(0xFF);
   SD_spiTxRx(0xFF);


   SD_deselect();


   if ((csd[0] & 0xC0U) == 0x40U) {
       uint32_t csize;


       csize = ((uint32_t)(csd[7] & 0x3FU) << 16) |
               ((uint32_t)csd[8] << 8) |
               ((uint32_t)csd[9]);


       *count = (csize + 1UL) << 10;
   } else {
       uint8_t read_bl_len;
       uint16_t c_size;
       uint8_t c_size_mult;
       uint32_t block_len;
       uint32_t mult;
       uint32_t blocknr;
       uint32_t capacity;


       read_bl_len = csd[5] & 0x0FU;
       c_size = ((uint16_t)(csd[6] & 0x03U) << 10) |
                ((uint16_t)csd[7] << 2) |
                ((csd[8] & 0xC0U) >> 6);


       c_size_mult = ((csd[9] & 0x03U) << 1) |
                     ((csd[10] & 0x80U) >> 7);


       block_len = 1UL << read_bl_len;
       mult = 1UL << (c_size_mult + 2U);
       blocknr = (uint32_t)(c_size + 1U) * mult;
       capacity = blocknr * block_len;


       *count = capacity / 512UL;
   }


   return SD_RES_OK;
}
