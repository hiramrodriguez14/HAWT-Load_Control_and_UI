#ifndef MCP45HV51_H_
#define MCP45HV51_H_

#include <stdint.h>
#include <stdbool.h>

/* I2C 7-bit addresses */
#define MCP45HV51_I2C_ADDR_A1A0_00      (0x3C)
#define MCP45HV51_I2C_ADDR_A1A0_01      (0x3D)
#define MCP45HV51_I2C_ADDR_A1A0_10      (0x3E)
#define MCP45HV51_I2C_ADDR_A1A0_11      (0x3F)

/* Default address: A1 = 0, A0 = 0 */
#define MCP45HV51_I2C_ADDR              MCP45HV51_I2C_ADDR_A1A0_00

/* Device constants */
#define MCP45HV51_RAB_OHMS              (10000UL)
#define MCP45HV51_MAX_CODE              (255U)

/* Public functions */
bool MCP45HV51_connectAllTerminals(void);

bool MCP45HV51_setWiperRaw(uint8_t code);

bool MCP45HV51_setPercent(uint8_t percent);

bool MCP45HV51_setRheostatWB_Ohms(uint32_t ohms);

bool MCP45HV51_setRheostatAW_Ohms(uint32_t ohms);

bool MCP45HV51_incrementWiper(void);

bool MCP45HV51_decrementWiper(void);

bool MCP45HV51_init(void);


#endif /* MCP45HV51_H_ */