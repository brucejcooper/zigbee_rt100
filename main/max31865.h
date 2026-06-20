/*
 * Minimal MAX31865 RTD-to-digital driver for ESP-IDF (SPI master).
 *
 * Runs the chip in automatic (continuous) conversion mode with VBIAS always on,
 * so the RTD register always holds a fresh sample and reads are cheap — suitable
 * for sampling several times per second.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAX31865_2WIRE = 0,
    MAX31865_3WIRE = 1,
    MAX31865_4WIRE = 0,
} max31865_wires_t;

typedef struct {
    spi_host_device_t host;     /*!< SPI host (e.g. SPI2_HOST) */
    int sclk_gpio;
    int miso_gpio;
    int mosi_gpio;
    int cs_gpio;
    int clock_hz;               /*!< SPI clock, <= 5 MHz */
    max31865_wires_t wires;     /*!< RTD wiring */
    bool filter_50hz;           /*!< true = 50 Hz mains notch, false = 60 Hz */
    float rtd_nominal_ohms;     /*!< RTD resistance at 0 C (100 or 1000) */
    float ref_ohms;             /*!< Reference resistor (430 or 4300) */
} max31865_config_t;

typedef struct max31865_dev_s *max31865_handle_t;

/* Fault bits as reported by the MAX31865 fault-status register (0x07). */
#define MAX31865_FAULT_HIGHTHRESH 0x80
#define MAX31865_FAULT_LOWTHRESH  0x40
#define MAX31865_FAULT_REFINLOW   0x20
#define MAX31865_FAULT_REFINHIGH  0x10
#define MAX31865_FAULT_RTDINLOW   0x08
#define MAX31865_FAULT_OVUV       0x04

/**
 * @brief Initialise the SPI bus (if needed), attach the MAX31865 and start
 *        continuous conversion.
 */
esp_err_t max31865_init(const max31865_config_t *cfg, max31865_handle_t *out_handle);

/**
 * @brief Read the raw 15-bit RTD ratio register.
 * @param[out] raw    15-bit RTD value (already shifted past the fault bit).
 * @param[out] fault  set true if the low fault bit in the RTD register is set.
 */
esp_err_t max31865_read_rtd(max31865_handle_t dev, uint16_t *raw, bool *fault);

/**
 * @brief Read a temperature in degrees Celsius using the Callendar-Van Dusen
 *        equation.
 */
esp_err_t max31865_read_temperature(max31865_handle_t dev, float *temp_c);

/**
 * @brief Read and clear the fault-status register (0x07).
 */
esp_err_t max31865_read_fault(max31865_handle_t dev, uint8_t *fault);
esp_err_t max31865_clear_fault(max31865_handle_t dev);

/**
 * @brief Log a full register dump (config, fault thresholds, RTD, fault) to
 *        prove SPI integrity. The fault-threshold registers default to
 *        0xFFFF / 0x0000 at power-up and are never written by this driver, so
 *        reading them back correctly confirms the bus is healthy.
 */
esp_err_t max31865_debug_dump(max31865_handle_t dev);

#ifdef __cplusplus
}
#endif
