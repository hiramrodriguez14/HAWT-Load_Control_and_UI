#include "ti_msp_dl_config.h"
#include "ina229.h"

#define INA229_SPI_TIMEOUT 1000000UL

/**
 * @brief Pulls the chip select (CS) line low to start an SPI transaction
 *
 * Activates the INA229 device by asserting CS. Required before any SPI transfer.
 *
 * @param dev Pointer to INA229 device structure
 */
static inline void ina229_cs_low(ina229_t *dev){
    DL_GPIO_clearPins((GPIO_Regs *)dev->cs_port, dev->cs_pin);
}

/**
 * @brief Releases the chip select (CS) line to end an SPI transaction
 *
 * Deactivates the INA229 device by deasserting CS after communication.
 *
 * @param dev Pointer to INA229 device structure
 */
static inline void ina229_cs_high(ina229_t *dev){
    DL_GPIO_setPins((GPIO_Regs *)dev->cs_port, dev->cs_pin);
}

/**
 * @brief Clears any pending data in the SPI RX FIFO
 *
 * Ensures that no stale data remains before starting a new SPI transaction.
 *
 * @param spi_inst SPI peripheral instance
 */
static void spi_flush_rx(void *spi_inst){
    while (!DL_SPI_isRXFIFOEmpty((SPI_Regs *)spi_inst)) {
        (void)DL_SPI_receiveData8((SPI_Regs *)spi_inst);
    }
} 

/**
 * @brief Performs a full-duplex SPI burst transfer, is the motor behing SPI communication
 *
 * Sends and receives multiple bytes over SPI while handling FIFO and timeout.
 *
 * @param spi_inst SPI peripheral instance
 * @param tx Pointer to transmit buffer
 * @param rx Pointer to receive buffer
 * @param len Number of bytes to transfer
 *
 * @return Status of the transaction (OK, TIMEOUT, or ARG ERROR)
 */
static ina229_status_t spi_transfer_burst(void *spi_inst, const uint8_t *tx, uint8_t *rx, uint32_t len){
    
    uint32_t tx_i = 0;
    uint32_t rx_i = 0;
    uint32_t timeout = INA229_SPI_TIMEOUT;

    if((tx == 0)||(rx == 0)||(len == 0)){
        return INA229_ERR_ARG;
    }

    while((rx_i < len) && (timeout > 0)){
        if(tx_i < len && (!DL_SPI_isTXFIFOFull((SPI_Regs *)spi_inst))){
            DL_SPI_transmitData8((SPI_Regs *)spi_inst, tx[tx_i]);
            tx_i++;
        }
        if(!DL_SPI_isRXFIFOEmpty((SPI_Regs *)spi_inst)){
            rx[rx_i] = DL_SPI_receiveData8((SPI_Regs *)spi_inst);
            rx_i++;
        }
        timeout--;
    }
    if(timeout == 0){
        return INA229_ERR_TIMEOUT;
    }
    timeout = INA229_SPI_TIMEOUT;
    while(DL_SPI_isBusy((SPI_Regs *)spi_inst) && (timeout > 0)){
        timeout--;
    }
    if(timeout == 0){
        return INA229_ERR_TIMEOUT;
    }
    return INA229_OK;
}

/**
 * @brief Sign-extends a 24-bit value to 32-bit signed integer
 *
 * Used for converting raw ADC data from INA229 registers.
 *
 * @param x Raw 24-bit value
 *
 * @return Signed 32-bit integer
 */
static inline int32_t ina229_sign_extend24(uint32_t x){
    if (x & 0x800000UL){
        x |= 0xFF000000UL;
    }
    return (int32_t)x;
}

/**
 * @brief Sign-extends a 40-bit value to 64-bit signed integer
 *
 * Used for converting extended precision measurements like charge or energy.
 *
 * @param x Raw 40-bit value
 *
 * @return Signed 64-bit integer
 */
static inline int64_t ina229_sign_extend40(uint64_t x){
    if (x & 0x8000000000ULL){
        x |= 0xFFFFFF0000000000ULL;
    }
    return (int64_t)x;
}

/**
 * @brief Initializes the INA229 device
 *
 * Validates parameters, reads configuration, extracts ADC range,
 * and writes the calculated shunt calibration value.
 *
 * @param dev Pointer to INA229 device structure
 *
 * @return Status of initialization
 */
ina229_status_t ina229_init(ina229_t *dev){

    uint16_t config;
    uint16_t shunt_cal;
    ina229_status_t status;

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    if(dev->spi_inst == 0){
        return INA229_ERR_ARG;
    }

    if(dev->r_shunt_ohms <= 0.0f){
        return INA229_ERR_ARG;
    }

    if(dev->current_lsb <= 0.0f){
        return INA229_ERR_ARG;
    }

    // CS in idle
    ina229_cs_high(dev);

    //Read CONFIG
    status = ina229_read_configuration(dev, &config);
    if(status != INA229_OK){
        return status;
    }

    //Store ADCRANGE
    dev->adc_range = (config >> 4) & 0x01;

    // Calculate SHUNT_CAL
    // Formula:
    // SHUNT_CAL = 13107.2e6 * current_lsb * Rshunt

    if(dev->adc_range == 1){ //If adc_range = 1 we must multiply the shunt calibration value by four, idk why I'm following datasheet orders
    shunt_cal = 4*((uint16_t)(13107200.0f * dev->current_lsb * dev->r_shunt_ohms));
    }else{
    shunt_cal = (uint16_t)(13107200.0f * dev->current_lsb * dev->r_shunt_ohms);
    }

    //Write SHUNT_CAL
    status = ina229_write_shunt_calibration(dev, shunt_cal);
    if(status != INA229_OK){
        return status;
    }

    return INA229_OK;
}

/**
 * @brief Reads a 16-bit register from INA229
 *
 * Performs an SPI transaction to retrieve register data.
 *
 * @param dev Pointer to INA229 device structure
 * @param reg Register address
 * @param value Pointer to store result
 *
 * @return Status of the operation
 */
ina229_status_t ina229_read_reg16(ina229_t *dev, uint8_t reg, uint16_t *value){

    uint8_t tx[3];
    uint8_t rx[3];
    ina229_status_t status;
    
    if((dev == 0)||(value == 0)){
        return INA229_ERR_ARG;
    }

    tx[0] = (uint8_t)(((reg & 0x3F) << 2) | 0x01); /*INA229 uses the 6 least significant bits for
    addresing the registers, we first apply the 0x3F mask only to check if the register we are adressing
    is in the existing range(0x3F is the last register), then we shift 2 positions left so the sixth 
    bit becomes the most significant one then, we have reg[6:0] + 0(reserved to 0 always) 
    + r/w bit(1 in this case for read)*/
    tx[1] = 0x00; //cero every byte from now on
    tx[2] = 0x00;

    spi_flush_rx(dev->spi_inst);

    ina229_cs_low(dev);
    status = spi_transfer_burst(dev->spi_inst, tx, rx, 3);
    ina229_cs_high(dev);

    if((status != INA229_OK)){
        return status;
    }

    *value = ((uint16_t)rx[1] << 8) |rx[2];
    return INA229_OK;
}

/**
 * @brief Writes a 16-bit value to an INA229 register
 *
 * Sends register address and data over SPI.
 *
 * @param dev Pointer to INA229 device structure
 * @param reg Register address
 * @param value Data to write
 *
 * @return Status of the operation
 */
ina229_status_t ina229_write_reg16(ina229_t *dev, uint8_t reg, uint16_t value){

    uint8_t tx[3];
    uint8_t rx[3];
    ina229_status_t status;
    
    if(dev == 0){
        return INA229_ERR_ARG;
    }

    tx[0] = (uint8_t)((reg & 0x3F) << 2); //write cmd [AAAAAA] + [0] + [0]
    tx[1] = (uint8_t)(value >> 8); //Data MSB first
    tx[2] = (uint8_t)(value & 0xFF); //Data LSB last

    spi_flush_rx(dev->spi_inst);

    ina229_cs_low(dev);
    status = spi_transfer_burst(dev->spi_inst, tx, rx, 3);
    ina229_cs_high(dev);

    return status;
}

/**
 * @brief Reads a 24-bit register from INA229
 *
 * Used for measurements like voltage and current.
 *
 * @param dev Pointer to INA229 device structure
 * @param reg Register address
 * @param value Pointer to store result
 *
 * @return Status of the operation
 */
ina229_status_t ina229_read_reg24(ina229_t *dev, uint8_t reg, uint32_t *value){

    uint8_t tx[4];
    uint8_t rx[4];
    ina229_status_t status;
    
    if((dev == 0)||(value == 0)){
        return INA229_ERR_ARG;
    }

    tx[0] = (uint8_t)(((reg & 0x3F) << 2) | 0x01);
    tx[1] = 0x00;
    tx[2] = 0x00;
    tx[3] = 0x00;

    spi_flush_rx(dev->spi_inst);

    ina229_cs_low(dev);
    status = spi_transfer_burst(dev->spi_inst, tx, rx, 4);
    ina229_cs_high(dev);

    if((status != INA229_OK)){
        return status;
    }

    *value = ((uint32_t)rx[1] << 16) |
    ((uint32_t)rx[2] << 8) |
    ((uint32_t)rx[3]);

    return INA229_OK;
}

/**
 * @brief Reads a 40-bit register from INA229
 *
 * Used for extended measurements such as energy and charge.
 *
 * @param dev Pointer to INA229 device structure
 * @param reg Register address
 * @param value Pointer to store result
 *
 * @return Status of the operation
 */
ina229_status_t ina229_read_reg40(ina229_t *dev, uint8_t reg, uint64_t *value){

    uint8_t tx[6];
    uint8_t rx[6];
    ina229_status_t status;
    
    if((dev == 0)||(value == 0)){
        return INA229_ERR_ARG;
    }

    tx[0] = (uint8_t)(((reg & 0x3F) << 2) | 0x01);
    tx[1] = 0x00;
    tx[2] = 0x00;
    tx[3] = 0x00;
    tx[4] = 0x00;
    tx[5] = 0x00;

    spi_flush_rx(dev->spi_inst);

    ina229_cs_low(dev);
    status = spi_transfer_burst(dev->spi_inst, tx, rx, 6);
    ina229_cs_high(dev);

    if((status != INA229_OK)){
        return status;
    }

    *value = ((uint64_t)rx[1] << 32) |
    ((uint64_t)rx[2] << 24) |
    ((uint64_t)rx[3] << 16) |
    ((uint64_t)rx[4] << 8) |
    ((uint64_t)rx[5]);
    
    return INA229_OK;
}

/**
 * @brief Reads manufacturer ID from INA229
 *
 * @param dev Pointer to INA229 device structure
 * @param value Pointer to store result
 *
 * @return Status of the operation
 */
ina229_status_t ina229_read_manufacturer_id(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_MANUFACTURER_ID, value);
}

/**
 * @brief Reads device ID from INA229
 */
ina229_status_t ina229_read_device_id(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_DEVICE_ID, value);
}

/**
 * @brief Reads configuration register
 */
ina229_status_t ina229_read_configuration(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_CONFIG, value);
}

/**
 * @brief Reads ADC configuration register
 */
ina229_status_t ina229_read_adc_configuration(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_ADC_CONFIG, value);
}

/**
 * @brief Reads shunt calibration register
 */
ina229_status_t ina229_read_shunt_calibration(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_SHUNT_CAL, value);
}

/**
 * @brief Reads shunt temperature coefficient
 */
ina229_status_t ina229_read_shunt_temperature_coefficient(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_SHUNT_TEMPCO, value);
}

/**
 * @brief Reads shunt voltage in volts
 *
 * Converts raw ADC data using the appropriate LSB depending on ADC range.
 */
ina229_status_t ina229_read_shunt_voltage(ina229_t *dev, float *value){

    uint32_t raw24;
    int32_t raw_signed;
    float lsb;
    
    ina229_status_t status;

    status = ina229_read_reg24(dev, INA229_REG_VSHUNT, &raw24);
    if(status != INA229_OK){
        return status;
    }

    raw_signed = ina229_sign_extend24(raw24);
    raw_signed = raw_signed >> 4; //Bits [23:4] validos ya que los bits [3:0] no se usan like estan reservados y leen 0

    if(dev->adc_range){
        lsb = 78.125e-9f;
    } else {
        lsb = 312.5e-9f;
    }   

    *value = (float)raw_signed * lsb;
    return INA229_OK;
}

/**
 * @brief Reads bus voltage in volts
 *
 * Converts raw register value to physical voltage.
 */
ina229_status_t ina229_read_bus_voltage(ina229_t *dev, float *value){

    uint32_t raw24;
    ina229_status_t status;

    status = ina229_read_reg24(dev, INA229_REG_VBUS, &raw24);
    if(status!= INA229_OK){
        return status;
    }

    raw24 = raw24 >> 4;
    *value = (float)raw24 * 195.3125e-6f;

    return INA229_OK;
}

/**
 * @brief Reads internal die temperature in degrees Celsius
 *
 * Converts raw register value using device scaling factor.
 */
ina229_status_t ina229_read_die_temperature(ina229_t *dev, float *value){

    uint16_t raw16;
    int16_t raw_signed;
    ina229_status_t status;

    status = ina229_read_reg16(dev, INA229_REG_DIETEMP, &raw16);
    if(status!=INA229_OK){
        return status;
    }

    raw_signed = (int16_t)raw16;
    *value = (float)raw_signed * 7.8125e-3f;

    return INA229_OK;
}

/**
 * @brief Reads current in amperes
 *
 * Uses current LSB scaling defined during initialization.
 */
ina229_status_t ina229_read_current(ina229_t *dev, float *value){

    uint32_t raw24;
    int32_t raw_signed;
    ina229_status_t status;

    if(dev->current_lsb <= 0.0f){
        return INA229_ERR_ARG;
    }

    status = ina229_read_reg24(dev, INA229_REG_CURRENT, &raw24);
    
    if(status!= INA229_OK){
        return status;
    }

    raw_signed = ina229_sign_extend24(raw24);
    raw_signed = raw_signed >> 4;

    *value = (float)raw_signed * dev->current_lsb;

    return INA229_OK;
}

/**
 * @brief Reads power in watts
 *
 * Converts raw value using device-defined scaling (3.2 × current_lsb).
 */
ina229_status_t ina229_read_power(ina229_t *dev, float *value){

    uint32_t raw24;
    ina229_status_t status;

    if(dev->current_lsb <= 0.0f){
        return INA229_ERR_ARG;
    }

    status = ina229_read_reg24(dev, INA229_REG_POWER, &raw24);
    
    if(status != INA229_OK){
        return status;
    }

    *value = (float)raw24 * (3.2f * dev->current_lsb);

    return INA229_OK;
}

/**
 * @brief Reads accumulated energy
 *
 * Converts raw 40-bit value using device scaling.
 */
ina229_status_t ina229_read_energy(ina229_t *dev, float *value){

    uint64_t raw40;
    ina229_status_t status;

    if(dev->current_lsb <= 0.0f){
        return INA229_ERR_ARG;
    }

    status = ina229_read_reg40(dev, INA229_REG_ENERGY, &raw40);

    if(status!=INA229_OK){
        return status;
    }

    *value = (float)raw40 * (16.0f * 3.2f * dev->current_lsb);

    return INA229_OK;
}

/**
 * @brief Reads accumulated charge
 *
 * Returns signed charge using current LSB scaling.
 */
ina229_status_t ina229_read_charge(ina229_t *dev, float *value){

    uint64_t raw40;
    int64_t raw_signed;
    ina229_status_t status;

    if(dev->current_lsb <= 0.0f){
        return INA229_ERR_ARG;
    }

    status = ina229_read_reg40(dev, INA229_REG_CHARGE, &raw40);
    
    if(status!=INA229_OK){
        return status;
    }

    raw_signed = ina229_sign_extend40(raw40);
    *value = (float)raw_signed * dev->current_lsb;

    return INA229_OK;
}

/**
 * @brief Reads diagnostic and alert flags
 */
ina229_status_t ina229_read_flags(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_DIAG_ALRT, value);
}

/**
 * @brief Reads shunt overvoltage threshold
 */
ina229_status_t ina229_read_shunt_overvoltage_threshold(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_SOVL, value);
}

/**
 * @brief Reads shunt undervoltage threshold
 */
ina229_status_t ina229_read_shunt_undervoltage_threshold(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_SUVL, value);
}

/**
 * @brief Reads bus overvoltage threshold
 */
ina229_status_t ina229_read_bus_overvoltage_threshold(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_BOVL, value);
}

/**
 * @brief Reads bus undervoltage threshold
 */
ina229_status_t ina229_read_bus_undervoltage_threshold(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_BUVL, value);
}

/**
 * @brief Reads temperature limit threshold
 */
ina229_status_t ina229_read_temperature_over_limit_threshold(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_TEMP_LIMIT, value);
}

/**
 * @brief Reads power limit threshold
 */
ina229_status_t ina229_read_power_over_limit_threshold(ina229_t *dev, uint16_t *value){
    return ina229_read_reg16(dev, INA229_REG_PWR_LIMIT, value);
}

/**
 * @brief Writes configuration register
 */
ina229_status_t ina229_write_configuration(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_CONFIG, value);
}

/**
 * @brief Writes ADC configuration register
 */
ina229_status_t ina229_write_adc_configuration(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_ADC_CONFIG, value);
}

/**
 * @brief Writes shunt calibration value
 */
ina229_status_t ina229_write_shunt_calibration(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_SHUNT_CAL, value);
}

/**
 * @brief Writes shunt temperature coefficient
 */
ina229_status_t ina229_write_shunt_temperature_coefficient(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_SHUNT_TEMPCO, value);
}

/**
 * @brief Writes diagnostic and alert flags
 */
ina229_status_t ina229_write_flags(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_DIAG_ALRT, value);
}

/**
 * @brief Writes shunt overvoltage threshold
 */
ina229_status_t ina229_write_shunt_overvoltage_threshold(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_SOVL, value);
}

/**
 * @brief Writes shunt undervoltage threshold
 */
ina229_status_t ina229_write_shunt_undervoltage_threshold(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_SUVL, value);
}

/**
 * @brief Writes bus overvoltage threshold
 */
ina229_status_t ina229_write_bus_overvoltage_threshold(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_BOVL, value);
}

/**
 * @brief Writes bus undervoltage threshold
 */
ina229_status_t ina229_write_bus_undervoltage_threshold(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_BUVL, value);
}

/**
 * @brief Writes temperature limit threshold
 */
ina229_status_t ina229_write_temperature_over_limit_threshold(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_TEMP_LIMIT, value);
}

/**
 * @brief Writes power limit threshold
 */
ina229_status_t ina229_write_power_over_limit_threshold(ina229_t *dev, uint16_t value){

    if(dev == 0){
        return INA229_ERR_ARG;
    }

    return ina229_write_reg16(dev, INA229_REG_PWR_LIMIT, value);
}


