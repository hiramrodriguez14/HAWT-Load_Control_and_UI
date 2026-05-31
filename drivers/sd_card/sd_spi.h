#ifndef SD_SPI_H_
#define SD_SPI_H_


#include <stdint.h>
#include <stdbool.h>


typedef enum {
   SD_RES_OK = 0,
   SD_RES_ERROR,
   SD_RES_TIMEOUT,
   SD_RES_BAD_TOKEN
} sd_result_t;




bool SD_init(void);
sd_result_t SD_readBlock(uint32_t sector, uint8_t *buf);
sd_result_t SD_writeBlock(uint32_t sector, const uint8_t *buf);
sd_result_t SD_getSectorCount(uint32_t *count);
uint8_t SD_getCardType(void);


#endif
