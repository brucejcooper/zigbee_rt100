/*
 * MAX31865 RTD-to-digital driver implementation. See max31865.h.
 */
#include "max31865.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "max31865";

/* Register addresses (read). Add 0x80 for the write address. */
#define REG_CONFIG       0x00
#define REG_RTD_MSB      0x01
#define REG_HFAULT_MSB   0x03
#define REG_LFAULT_MSB   0x05
#define REG_FAULT_STATUS 0x07
#define REG_WRITE_BIT    0x80

/* Config register bits (0x00). */
#define CFG_BIAS         0x80
#define CFG_MODE_AUTO    0x40
#define CFG_ONE_SHOT     0x20
#define CFG_3WIRE        0x10
#define CFG_FAULT_CLEAR  0x02
#define CFG_FILTER_50HZ  0x01

/* Callendar-Van Dusen coefficients (IEC 60751). */
#define RTD_A 3.9083e-3f
#define RTD_B (-5.775e-7f)

struct max31865_dev_s {
    spi_device_handle_t spi;
    float rtd_nominal_ohms;
    float ref_ohms;
};

static esp_err_t reg_read(max31865_handle_t dev, uint8_t addr, uint8_t *buf, size_t len)
{
    /* First TX byte = address, subsequent bytes clock out the registers. */
    uint8_t tx[16] = {0};
    uint8_t rx[16] = {0};
    if (len + 1 > sizeof(tx)) {
        return ESP_ERR_INVALID_SIZE;
    }
    tx[0] = addr & 0x7F; /* read: top bit clear */
    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_polling_transmit(dev->spi, &t);
    if (err != ESP_OK) {
        return err;
    }
    memcpy(buf, &rx[1], len);
    return ESP_OK;
}

static esp_err_t reg_write8(max31865_handle_t dev, uint8_t addr, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(addr | REG_WRITE_BIT), value };
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

esp_err_t max31865_init(const max31865_config_t *cfg, max31865_handle_t *out_handle)
{
    if (!cfg || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    /* The SPI bus may already be initialised by another driver; tolerate that. */
    spi_bus_config_t buscfg = {
        .sclk_io_num = cfg->sclk_gpio,
        .miso_io_num = cfg->miso_gpio,
        .mosi_io_num = cfg->mosi_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    esp_err_t err = spi_bus_initialize(cfg->host, &buscfg, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    /* MAX31865 is SPI mode 1 (CPOL=0, CPHA=1). CS is active low. */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = cfg->clock_hz,
        .mode = 1,
        .spics_io_num = cfg->cs_gpio,
        .queue_size = 1,
        .cs_ena_posttrans = 1,
    };

    struct max31865_dev_s *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }
    dev->rtd_nominal_ohms = cfg->rtd_nominal_ohms;
    dev->ref_ohms = cfg->ref_ohms;

    err = spi_bus_add_device(cfg->host, &devcfg, &dev->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        free(dev);
        return err;
    }

    /* Build the config: bias on, automatic conversion, wiring, mains filter.
     * NB: the 50/60 Hz filter bit must only be changed while auto-conversion is
     * off, so we write it here before anything else runs. */
    uint8_t config = CFG_BIAS | CFG_MODE_AUTO | CFG_FAULT_CLEAR;
    if (cfg->wires == MAX31865_3WIRE) {
        config |= CFG_3WIRE;
    }
    if (cfg->filter_50hz) {
        config |= CFG_FILTER_50HZ;
    }
    err = reg_write8(dev, REG_CONFIG, config);
    if (err != ESP_OK) {
        spi_bus_remove_device(dev->spi);
        free(dev);
        return err;
    }

    /* Verify we can talk to the chip: read config back. */
    uint8_t readback = 0;
    err = reg_read(dev, REG_CONFIG, &readback, 1);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MAX31865 online (config wrote 0x%02x, read 0x%02x)", config, readback);
        if (readback == 0x00 || readback == 0xFF) {
            ESP_LOGW(TAG, "Config readback looks wrong (0x%02x) - check wiring/CS", readback);
        }
    }

    *out_handle = dev;
    return ESP_OK;
}

esp_err_t max31865_read_rtd(max31865_handle_t dev, uint16_t *raw, bool *fault)
{
    if (!dev || !raw) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buf[2] = {0};
    esp_err_t err = reg_read(dev, REG_RTD_MSB, buf, 2);
    if (err != ESP_OK) {
        return err;
    }
    uint16_t rtd = ((uint16_t)buf[0] << 8) | buf[1];
    if (fault) {
        *fault = rtd & 0x0001;
    }
    *raw = rtd >> 1; /* drop the fault bit -> 15-bit ratio */
    return ESP_OK;
}

esp_err_t max31865_read_temperature(max31865_handle_t dev, float *temp_c)
{
    if (!dev || !temp_c) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t raw = 0;
    bool fault = false;
    esp_err_t err = max31865_read_rtd(dev, &raw, &fault);
    if (err != ESP_OK) {
        return err;
    }
    if (fault) {
        /* The RTD value is unreliable. Leave the fault latched so the caller can
         * read the detailed fault-status register and report it before clearing. */
        return ESP_ERR_INVALID_STATE;
    }

    /* Convert ratio -> resistance. */
    float rt = ((float)raw / 32768.0f) * dev->ref_ohms;

    /* Callendar-Van Dusen, positive-temperature branch (Adafruit-style). */
    float z1 = -RTD_A;
    float z2 = RTD_A * RTD_A - (4.0f * RTD_B);
    float z3 = (4.0f * RTD_B) / dev->rtd_nominal_ohms;
    float z4 = 2.0f * RTD_B;

    float temp = z2 + (z3 * rt);
    temp = (sqrtf(temp) + z1) / z4;
    if (temp >= 0.0f) {
        *temp_c = temp;
        return ESP_OK;
    }

    /* Below 0 C the quadratic is inaccurate; use the polynomial approximation
     * normalised to a PT100-equivalent resistance. */
    float r = (rt / dev->rtd_nominal_ohms) * 100.0f;
    float poly = r;
    temp = -242.02f;
    temp += 2.2228f * poly;
    poly *= r;
    temp += 2.5859e-3f * poly;
    poly *= r;
    temp -= 4.8260e-6f * poly;
    poly *= r;
    temp -= 2.8183e-8f * poly;
    poly *= r;
    temp += 1.5243e-10f * poly;

    *temp_c = temp;
    return ESP_OK;
}

esp_err_t max31865_read_fault(max31865_handle_t dev, uint8_t *fault)
{
    if (!dev || !fault) {
        return ESP_ERR_INVALID_ARG;
    }
    return reg_read(dev, REG_FAULT_STATUS, fault, 1);
}

esp_err_t max31865_clear_fault(max31865_handle_t dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Re-assert the running config with the fault-clear bit pulsed. Reading the
     * current config first keeps wiring/filter/mode settings intact. */
    uint8_t config = 0;
    esp_err_t err = reg_read(dev, REG_CONFIG, &config, 1);
    if (err != ESP_OK) {
        return err;
    }
    config &= ~0x2C;           /* clear D2-D3 (fault detect) and D5 (1-shot) */
    config |= CFG_FAULT_CLEAR; /* D1: clear fault status */
    return reg_write8(dev, REG_CONFIG, config);
}

esp_err_t max31865_debug_dump(max31865_handle_t dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Read the whole register bank in one auto-incrementing burst (0x00..0x07). */
    uint8_t r[8] = {0};
    esp_err_t err = reg_read(dev, REG_CONFIG, r, 8);
    if (err != ESP_OK) {
        return err;
    }
    uint16_t hft = ((uint16_t)r[3] << 8) | r[4];
    uint16_t lft = ((uint16_t)r[5] << 8) | r[6];
    uint16_t rtd = ((uint16_t)r[1] << 8) | r[2];
    ESP_LOGI(TAG,
             "REGDUMP cfg=0x%02x RTD=0x%04x HighFaultThr=0x%04x LowFaultThr=0x%04x fault=0x%02x",
             r[0], rtd, hft, lft, r[7]);
    ESP_LOGI(TAG,
             "  -> SPI check: HighFaultThr should be 0xFFFF, LowFaultThr 0x0000 at power-up "
             "(registers we never write). Match => bus is healthy.");
    return ESP_OK;
}
