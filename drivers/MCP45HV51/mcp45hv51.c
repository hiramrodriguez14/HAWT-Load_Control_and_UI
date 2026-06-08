/*
 *
 * Wiring:
 *   MCP45HV51 pin     Connection
 *   ---------------------------------------------
 *   VL                3.3V from LaunchPad
 *   DGND              GND from LaunchPad
 *   SCL               PB2 / I2C1_SCL
 *   SDA               PB3 / I2C1_SDA
 *   A0                GND  -> address bit 0 = 0
 *   A1                GND  -> address bit 1 = 0
 *   WLAT              GND  -> wiper updates immediately
 *   SHDN              3.3V -> normal mode, shutdown disabled
 *   V-                analog low reference, often GND
 *   V+                analog high supply, 10V to 36V for specified operation
 *   P0A/P0W/P0B       connect according to your circuit
 *
 * Notes:
 *   - This part is VOLATILE. After power reset, the wiper returns to mid-scale.
 *   - For MCP45HV51, code 0x00 is closest to terminal B.
 *   - For MCP45HV51, code 0xFF is closest to terminal A.
 *   - If using as rheostat between P0W and P0B:
 *        0x00 -> minimum resistance
 *        0xFF -> maximum resistance, about 10k plus nonideal effects
 *   - If using as rheostat between P0A and P0W:
 *        0x00 -> maximum resistance
 *        0xFF -> minimum resistance
 */

#include "mcp45hv51.h"
#include "ti_msp_dl_config.h"

/* ============================================================
 * MCP45HV51-103E/ST configuration
 * ============================================================ */

/*
 * MCP45HVX1 I2C 7-bit address:
 *
 * Control byte in datasheet is:
 *   0b0111 1 A1 A0 R/W
 *
 * The DriverLib function wants only the 7-bit address, without R/W.
 *
 * A1=0, A0=0 -> 0b0111100 = 0x3C
 * A1=0, A0=1 -> 0b0111101 = 0x3D
 * A1=1, A0=0 -> 0b0111110 = 0x3E
 * A1=1, A0=1 -> 0b0111111 = 0x3F
 */
#define MCP45HV51_I2C_ADDR_A1A0_00      (0x3C)
#define MCP45HV51_I2C_ADDR_A1A0_01      (0x3D)
#define MCP45HV51_I2C_ADDR_A1A0_10      (0x3E)
#define MCP45HV51_I2C_ADDR_A1A0_11      (0x3F)

/*
 * Change this if your A0/A1 pins are not both connected to GND.
 */
#define MCP45HV51_I2C_ADDR              MCP45HV51_I2C_ADDR_A1A0_00

/*
 * MCP45HV51 memory addresses.
 */
#define MCP45HV51_MEM_VOLATILE_WIPER0   (0x00)
#define MCP45HV51_MEM_TCON0             (0x04)

/*
 * MCP45HV51 command operation bits C1:C0.
 */
#define MCP45HV51_CMD_WRITE             (0x00)
#define MCP45HV51_CMD_INCREMENT         (0x01)
#define MCP45HV51_CMD_DECREMENT         (0x02)
#define MCP45HV51_CMD_READ              (0x03)

/*
 * TCON register default:
 *   bit 3 R0HW = 1 -> not forced into hardware shutdown configuration
 *   bit 2 R0A  = 1 -> terminal A connected
 *   bit 1 R0W  = 1 -> wiper W connected
 *   bit 0 R0B  = 1 -> terminal B connected
 * Upper bits read/force as 1.
 */
#define MCP45HV51_TCON_ALL_CONNECTED    (0xFF)

/*
 * MCP45HV51-103 is 10k nominal and 8-bit.
 */
#define MCP45HV51_RAB_OHMS              (10000UL)
#define MCP45HV51_MAX_CODE              (255U)

/* ============================================================
 * Globals
 * ============================================================ */

static uint32_t gI2CDelayCycles = 0;


static void i2c_calculate_delay_cycles(void)
{
    DL_I2C_ClockConfig i2cClockConfig;
    uint32_t clockSelFreq = 32000000U;

    DL_I2C_getClockConfig(MCP45HV51_INST, &i2cClockConfig);

    switch (i2cClockConfig.clockSel) {
        case DL_I2C_CLOCK_BUSCLK:
            clockSelFreq = 32000000U;
            break;

        case DL_I2C_CLOCK_MFCLK:
            clockSelFreq = 4000000U;
            break;

        default:
            clockSelFreq = 32000000U;
            break;
    }

    /*
     * Workaround style used by TI's I2C FIFO polling example.
     * Delay = 3 I2C functional clock cycles after starting transfer.
     */
    gI2CDelayCycles = (3U * (i2cClockConfig.divideRatio + 1U)) *
                      (CPUCLK_FREQ / clockSelFreq);
}

static uint8_t mcp45hv51_make_command(uint8_t memoryAddress, uint8_t command)
{
    /*
     * MCP45HVX1 command byte format:
     *
     *   AD3 AD2 AD1 AD0 C1 C0 D9 D8
     *
     * For this 8-bit device, D9:D8 are ignored, so they are written as 0.
     */
    return (uint8_t)(((memoryAddress & 0x0FU) << 4) |
                     ((command & 0x03U) << 2));
}

static bool i2c_controller_write(uint8_t targetAddress,
                                 const uint8_t *data,
                                 uint8_t length)
{
    if ((data == 0) || (length == 0U) || (length > 8U)) {
        return false;
    }

    while (!(DL_I2C_getControllerStatus(MCP45HV51_INST) &
             DL_I2C_CONTROLLER_STATUS_IDLE)) {
        ;
    }

    DL_I2C_fillControllerTXFIFO(MCP45HV51_INST, data, length);

    DL_I2C_startControllerTransfer(MCP45HV51_INST,
                                   targetAddress,
                                   DL_I2C_CONTROLLER_DIRECTION_TX,
                                   length);

    /*
     * Workaround for I2C_ERR_13, same idea as the TI polling example.
     */
    delay_cycles(gI2CDelayCycles);

    while (DL_I2C_getControllerStatus(MCP45HV51_INST) &
           DL_I2C_CONTROLLER_STATUS_BUSY) {
        ;
    }

    if (DL_I2C_getControllerStatus(MCP45HV51_INST) &
        DL_I2C_CONTROLLER_STATUS_ERROR) {
        return false;
    }

    return true;
}

/* ============================================================
 * MCP45HV51 functions
 * ============================================================ */
bool MCP45HV51_init(void)
{
    i2c_calculate_delay_cycles();

    if (!MCP45HV51_connectAllTerminals()) {
        return false;
    }

    return true;
}
/*
 * Reconnect all internal terminals using TCON.
 * This is useful at startup, especially if you previously tested shutdown/TCON.
 */
bool MCP45HV51_connectAllTerminals(void)
{
    uint8_t packet[2];

    packet[0] = mcp45hv51_make_command(MCP45HV51_MEM_TCON0,
                                        MCP45HV51_CMD_WRITE);
    packet[1] = MCP45HV51_TCON_ALL_CONNECTED;

    return i2c_controller_write(MCP45HV51_I2C_ADDR, packet, 2U);
}

/*
 * Write the raw 8-bit wiper code.
 *
 * code = 0x00 -> W near B
 * code = 0x7F -> about middle
 * code = 0xFF -> W near A
 */
bool MCP45HV51_setWiperRaw(uint8_t code)
{
    uint8_t packet[2];

    packet[0] = mcp45hv51_make_command(MCP45HV51_MEM_VOLATILE_WIPER0,
                                        MCP45HV51_CMD_WRITE);
    packet[1] = code;

    return i2c_controller_write(MCP45HV51_I2C_ADDR, packet, 2U);
}

/*
 * Set wiper by percent.
 *
 * percent = 0   -> code 0x00
 * percent = 50  -> code around 0x7F
 * percent = 100 -> code 0xFF
 */
bool MCP45HV51_setPercent(uint8_t percent)
{
    uint16_t code;

    if (percent > 100U) {
        percent = 100U;
    }

    code = ((uint16_t)percent * MCP45HV51_MAX_CODE) / 100U;

    return MCP45HV51_setWiperRaw((uint8_t)code);
}

/*
 * Set approximate rheostat resistance between P0W and P0B.
 *
 * This assumes you are using the part as:
 *   resistor between W and B
 *
 * This is ideal/approximate. Real value has tolerance and wiper resistance.
 */
bool MCP45HV51_setRheostatWB_Ohms(uint32_t ohms)
{
    uint32_t code;

    if (ohms > MCP45HV51_RAB_OHMS) {
        ohms = MCP45HV51_RAB_OHMS;
    }

    code = (ohms * MCP45HV51_MAX_CODE) / MCP45HV51_RAB_OHMS;

    return MCP45HV51_setWiperRaw((uint8_t)code);
}

/*
 * Set approximate rheostat resistance between P0A and P0W.
 *
 * This assumes you are using the part as:
 *   resistor between A and W
 *
 * Direction is inverted compared with W-B.
 */
bool MCP45HV51_setRheostatAW_Ohms(uint32_t ohms)
{
    uint32_t code;

    if (ohms > MCP45HV51_RAB_OHMS) {
        ohms = MCP45HV51_RAB_OHMS;
    }

    code = MCP45HV51_MAX_CODE -
           ((ohms * MCP45HV51_MAX_CODE) / MCP45HV51_RAB_OHMS);

    return MCP45HV51_setWiperRaw((uint8_t)code);
}

/*
 * Increment wiper by 1 tap.
 * At 0xFF, the device ignores further increment commands.
 */
bool MCP45HV51_incrementWiper(void)
{
    uint8_t commandByte;

    commandByte = mcp45hv51_make_command(MCP45HV51_MEM_VOLATILE_WIPER0,
                                         MCP45HV51_CMD_INCREMENT);

    return i2c_controller_write(MCP45HV51_I2C_ADDR, &commandByte, 1U);
}

/*
 * Decrement wiper by 1 tap.
 * At 0x00, the device ignores further decrement commands.
 */
bool MCP45HV51_decrementWiper(void)
{
    uint8_t commandByte;

    commandByte = mcp45hv51_make_command(MCP45HV51_MEM_VOLATILE_WIPER0,
                                         MCP45HV51_CMD_DECREMENT);

    return i2c_controller_write(MCP45HV51_I2C_ADDR, &commandByte, 1U);
}
