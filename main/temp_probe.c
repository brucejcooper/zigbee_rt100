/*
 * Zigbee Router temperature probe for the Seeed XIAO ESP32-C6 with an
 * Adafruit MAX31865 RTD amplifier on the standard SPI pins.
 *
 * - Joins a Zigbee network as a mains-powered Router (ZR).
 * - Exposes a Home Automation Temperature Sensor endpoint (cluster 0x0402).
 * - Samples the RTD locally and publishes MeasuredValue. The reporting rate is
 *   client-configurable via the standard ZCL Configure Reporting command
 *   (1 s granularity / 1 s floor). An optional fast push-report mode can exceed
 *   1 Hz by sending unsolicited Report Attributes commands.
 * - Reports MAX31865 fault conditions two ways: a standard Binary Input cluster
 *   (interoperable "RTD fault" flag) and a manufacturer-specific diagnostics
 *   cluster carrying the raw fault byte plus the static sensor configuration.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "math.h"
#if CONFIG_TP_ANTENNA_AB_TEST
#include "esp_ieee802154.h"
#endif

#include "ha/esp_zigbee_ha_standard.h"
#include "zcl_utility.h"

#include "temp_probe.h"
#include "max31865.h"

#if !defined ZB_COORDINATOR_ROLE
#error "Build the ZCZR library: set CONFIG_ZB_ZCZR=y (Components -> Zigbee). Router role is selected at runtime."
#endif

static const char *TAG = "TEMP_PROBE";

static max31865_handle_t s_rtd;
static bool s_joined = false;

/* ----- MAX31865 wiring selected from Kconfig ------------------------------ */
#if defined(CONFIG_TP_RTD_3WIRE)
#define TP_WIRES      MAX31865_3WIRE
#define TP_WIRE_COUNT 3
#elif defined(CONFIG_TP_RTD_4WIRE)
#define TP_WIRES      MAX31865_4WIRE
#define TP_WIRE_COUNT 4
#else
#define TP_WIRES      MAX31865_2WIRE
#define TP_WIRE_COUNT 2
#endif

/* Backing store for the manufacturer diagnostics-cluster attributes. The stack
 * copies these at registration; the fault byte is updated live at runtime. */
static uint8_t  s_diag_fault_raw = 0;
static uint8_t  s_diag_wires     = TP_WIRE_COUNT;
static uint16_t s_diag_nominal   = CONFIG_TP_RTD_NOMINAL_OHMS;
static uint16_t s_diag_ref       = CONFIG_TP_RTD_REF_OHMS;
static bool     s_diag_filter50  = CONFIG_TP_RTD_FILTER_50HZ;
static uint16_t s_diag_sample_ms = CONFIG_TP_SAMPLE_PERIOD_MS;

/* Antenna selection (true = external U.FL, false = on-board ceramic). The Kconfig
 * value is the power-on default; a Zigbee write to TP_DIAG_ATTR_ANTENNA overrides
 * it at runtime and is persisted to NVS so it survives reboots. */
#if CONFIG_TP_USE_EXTERNAL_ANTENNA
#define TP_ANTENNA_DEFAULT true
#else
#define TP_ANTENNA_DEFAULT false
#endif
#define TP_NVS_NAMESPACE "tempprobe"
#define TP_NVS_KEY_ANT   "ant_ext"
static bool s_diag_antenna_ext = TP_ANTENNA_DEFAULT; /* backing store for the attr */

/* ------------------------------------------------------------------------- */
/* Antenna control. The XIAO ESP32-C6 routes RF through an on-board switch:     */
/* GPIO3 (active LOW) powers the switch, GPIO14 selects the antenna (LOW =      */
/* on-board ceramic, HIGH = external U.FL). The selection is settable over      */
/* Zigbee (TP_DIAG_ATTR_ANTENNA) and persisted in NVS.                          */
/* ------------------------------------------------------------------------- */
static bool antenna_load_pref(void)
{
    bool external = TP_ANTENNA_DEFAULT;
    nvs_handle_t h;
    if (nvs_open(TP_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint8_t stored = 0;
        if (nvs_get_u8(h, TP_NVS_KEY_ANT, &stored) == ESP_OK) {
            external = (stored != 0);
        }
        nvs_close(h);
    }
    return external;
}

static void antenna_save_pref(bool external)
{
    nvs_handle_t h;
    if (nvs_open(TP_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, TP_NVS_KEY_ANT, external ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* Drive GPIO14 to the requested antenna. GPIO3 is held low (switch powered). */
static void antenna_apply(bool external)
{
    gpio_set_level(CONFIG_TP_ANT_RF_SWITCH_EN_GPIO, 0);
    gpio_set_level(CONFIG_TP_ANT_SELECT_GPIO, external ? 1 : 0);
    s_diag_antenna_ext = external;
}

/* Boot-time setup: configure the GPIOs and apply the persisted selection.
 * Must run before the radio transmits. */
static void configure_antenna(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << CONFIG_TP_ANT_RF_SWITCH_EN_GPIO) |
                        (1ULL << CONFIG_TP_ANT_SELECT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    bool external = antenna_load_pref();
    antenna_apply(external);
    ESP_LOGI(TAG, "Antenna: %s (RF-switch GPIO%d=0, select GPIO%d=%d)",
             external ? "external U.FL" : "on-board ceramic",
             CONFIG_TP_ANT_RF_SWITCH_EN_GPIO, CONFIG_TP_ANT_SELECT_GPIO, external ? 1 : 0);
}

/* Runtime change requested over Zigbee: apply, persist, log. */
static void antenna_set(bool external)
{
    antenna_apply(external);
    antenna_save_pref(external);
    ESP_LOGW(TAG, "Antenna switched to %s via Zigbee (persisted)",
             external ? "external U.FL" : "on-board ceramic");
}

#if CONFIG_TP_ANTENNA_AB_TEST
/* Antenna A/B RSSI self-test. Cycles between antennas, stimulating downlink
 * traffic (each report draws an APS ack back from the coordinator) and sampling
 * the RSSI/LQI of frames this device receives from its parent. recent_rssi
 * reflects how strongly WE hear the parent, which is what our antenna changes. */
static void ab_send_report(void)
{
    esp_zb_zcl_report_attr_cmd_t r = {
        .zcl_basic_cmd.src_endpoint = HA_TEMP_SENSOR_ENDPOINT,
        .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        .clusterID = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .attributeID = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
    };
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_report_attr_cmd_req(&r);
    esp_zb_lock_release();
}

static void antenna_ab_test_task(void *arg)
{
    const int rounds = 3;
    const int window_ms = 12000;
    const int step_ms = 400;
    vTaskDelay(pdMS_TO_TICKS(8000)); /* let the network settle after boot */
    ESP_LOGW(TAG, "=== Antenna A/B RSSI test starting (%d rounds) ===", rounds);

    for (int round = 1; round <= rounds; round++) {
        for (int ext = 1; ext >= 0; ext--) {
            antenna_apply(ext != 0);
            vTaskDelay(pdMS_TO_TICKS(1500)); /* RF settle */
            long rssi_sum = 0, lqi_sum = 0;
            int n = 0, rmin = 127, rmax = -128;
            for (int t = 0; t < window_ms; t += step_ms) {
                ab_send_report();
                vTaskDelay(pdMS_TO_TICKS(step_ms));
                int8_t rssi = esp_ieee802154_get_recent_rssi();
                uint8_t lqi = esp_ieee802154_get_recent_lqi();
                if (rssi <= 0 && rssi >= -120) { /* plausible dBm */
                    rssi_sum += rssi; lqi_sum += lqi; n++;
                    if (rssi < rmin) rmin = rssi;
                    if (rssi > rmax) rmax = rssi;
                }
            }
            if (n > 0) {
                ESP_LOGW(TAG, "AB r%d %-8s: RSSI avg=%.1f dBm (min=%d max=%d)  LQI avg=%ld  samples=%d",
                         round, ext ? "EXTERNAL" : "INTERNAL",
                         (double)rssi_sum / n, rmin, rmax, lqi_sum / n, n);
            } else {
                ESP_LOGW(TAG, "AB r%d %-8s: NO frames received (link likely lost on this antenna)",
                         round, ext ? "EXTERNAL" : "INTERNAL");
            }
        }
    }
    antenna_apply(antenna_load_pref()); /* restore persisted selection */
    ESP_LOGW(TAG, "=== Antenna A/B RSSI test complete; antenna restored ===");
    vTaskDelete(NULL);
}
#endif /* CONFIG_TP_ANTENNA_AB_TEST */

/* ------------------------------------------------------------------------- */
/* Fault reporting: update the standard Binary Input flag and the diagnostics  */
/* cluster's raw fault byte, then push an edge-triggered report. Caller holds   */
/* the Zigbee lock.                                                            */
/* ------------------------------------------------------------------------- */
static void publish_fault_state(uint8_t fault_raw)
{
    static bool s_have_last = false;
    static uint8_t s_last_raw = 0;
    if (s_have_last && fault_raw == s_last_raw) {
        return; /* edge-triggered: nothing changed */
    }
    s_have_last = true;
    s_last_raw = fault_raw;

    bool fault_active = (fault_raw != 0);
    uint8_t status_flag = fault_active ? TP_BI_STATUS_FLAG_FAULT : 0;

    esp_zb_zcl_set_attribute_val(HA_TEMP_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID, &fault_active, false);
    esp_zb_zcl_set_attribute_val(HA_TEMP_SENSOR_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ESP_ZB_ZCL_ATTR_BINARY_INPUT_STATUS_FLAGS_ID, &status_flag, false);
    esp_zb_zcl_set_attribute_val(HA_TEMP_SENSOR_ENDPOINT, TP_DIAG_CLUSTER_ID,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 TP_DIAG_ATTR_FAULT_RAW, &fault_raw, false);

    if (fault_active) {
        ESP_LOGW(TAG, "MAX31865 fault: 0x%02x%s%s%s%s%s%s", fault_raw,
                 (fault_raw & MAX31865_FAULT_HIGHTHRESH) ? " RTD>high" : "",
                 (fault_raw & MAX31865_FAULT_LOWTHRESH)  ? " RTD<low" : "",
                 (fault_raw & MAX31865_FAULT_REFINLOW)   ? " REFIN-<0.85*Vbias" : "",
                 (fault_raw & MAX31865_FAULT_REFINHIGH)  ? " REFIN->0.85*Vbias" : "",
                 (fault_raw & MAX31865_FAULT_RTDINLOW)   ? " RTDIN-<0.85*Vbias" : "",
                 (fault_raw & MAX31865_FAULT_OVUV)       ? " over/under-volt" : "");
    } else {
        ESP_LOGI(TAG, "MAX31865 fault cleared");
    }

    if (!s_joined) {
        return; /* nothing to send to yet */
    }
    /* Edge-triggered unsolicited reports so a fault is delivered immediately,
     * independent of the configured reporting intervals. Goes to bound nodes. */
    esp_zb_zcl_report_attr_cmd_t bi_report = {
        .zcl_basic_cmd.src_endpoint = HA_TEMP_SENSOR_ENDPOINT,
        .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        .clusterID = ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .attributeID = ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID,
    };
    esp_zb_zcl_report_attr_cmd_req(&bi_report);

    esp_zb_zcl_report_attr_cmd_t diag_report = {
        .zcl_basic_cmd.src_endpoint = HA_TEMP_SENSOR_ENDPOINT,
        .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        .clusterID = TP_DIAG_CLUSTER_ID,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .attributeID = TP_DIAG_ATTR_FAULT_RAW,
    };
    esp_zb_zcl_report_attr_cmd_req(&diag_report);
}

/* ------------------------------------------------------------------------- */
/* Sensor sampling task: read the RTD + fault register, push MeasuredValue and  */
/* fault state into the clusters. Updating the temperature attribute drives the */
/* stack's standard reporting machinery; the optional fast path fires extra      */
/* unsolicited reports for sub-second publishing.                              */
/* ------------------------------------------------------------------------- */
static void sensor_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(CONFIG_TP_SAMPLE_PERIOD_MS);
#if CONFIG_TP_FAST_REPORT_PERIOD_MS > 0
    uint32_t ms_since_fast_report = 0;
#endif

    for (;;) {
        /* Read the detailed fault register first; max31865_read_temperature
         * deliberately leaves a flagged fault latched so we capture it here. */
        uint8_t fault_raw = 0;
        max31865_read_fault(s_rtd, &fault_raw);

        float temp_c = 0.0f;
        esp_err_t terr = max31865_read_temperature(s_rtd, &temp_c);

        esp_zb_lock_acquire(portMAX_DELAY);

        if (terr == ESP_OK) {
            int16_t centi = (int16_t)lroundf(temp_c * 100.0f);
            esp_zb_zcl_set_attribute_val(HA_TEMP_SENSOR_ENDPOINT,
                                         ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                         ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
                                         &centi, false);
#if CONFIG_TP_FAST_REPORT_PERIOD_MS > 0
            ms_since_fast_report += CONFIG_TP_SAMPLE_PERIOD_MS;
            if (s_joined && ms_since_fast_report >= CONFIG_TP_FAST_REPORT_PERIOD_MS) {
                ms_since_fast_report = 0;
                esp_zb_zcl_report_attr_cmd_t report = {
                    .zcl_basic_cmd.src_endpoint = HA_TEMP_SENSOR_ENDPOINT,
                    .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
                    .clusterID = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                    .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
                    .attributeID = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
                };
                esp_zb_zcl_report_attr_cmd_req(&report);
            }
#endif
            ESP_LOGD(TAG, "RTD = %.2f C (MeasuredValue=%d)", temp_c, centi);
        }

        publish_fault_state(fault_raw);
        esp_zb_lock_release();

        /* Clear so the next cycle re-latches a fresh fault snapshot. In auto
         * conversion mode any persisting fault re-asserts immediately. */
        if (fault_raw != 0 || terr == ESP_ERR_INVALID_STATE) {
            max31865_clear_fault(s_rtd);
        }

        vTaskDelay(period);
    }
}

static esp_err_t deferred_driver_init(void)
{
    static bool initialised = false;
    if (initialised) {
        return ESP_OK;
    }
    max31865_config_t cfg = {
        .host = SPI2_HOST,
        .sclk_gpio = CONFIG_TP_SPI_SCLK_GPIO,
        .miso_gpio = CONFIG_TP_SPI_MISO_GPIO,
        .mosi_gpio = CONFIG_TP_SPI_MOSI_GPIO,
        .cs_gpio = CONFIG_TP_SPI_CS_GPIO,
        .clock_hz = CONFIG_TP_SPI_CLOCK_HZ,
        .wires = TP_WIRES,
        .filter_50hz = CONFIG_TP_RTD_FILTER_50HZ,
        .rtd_nominal_ohms = (float)CONFIG_TP_RTD_NOMINAL_OHMS,
        .ref_ohms = (float)CONFIG_TP_RTD_REF_OHMS,
    };
    ESP_RETURN_ON_ERROR(max31865_init(&cfg, &s_rtd), TAG, "MAX31865 init failed");

    BaseType_t ok = xTaskCreate(sensor_task, "rtd_sample", 4096, NULL, 4, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "Failed to start sensor task");

#if CONFIG_TP_ANTENNA_AB_TEST
    xTaskCreate(antenna_ab_test_task, "ant_ab", 4096, NULL, 3, NULL);
#endif

    initialised = true;
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* Reporting configuration: advertise sensible defaults so a coordinator that  */
/* does not send its own Configure Reporting still gets periodic updates.      */
/* A client can override min/max interval and reportable change at runtime.    */
/* ------------------------------------------------------------------------- */
static void configure_reporting(void)
{
    esp_zb_zcl_reporting_info_t temp_info = {
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .ep = HA_TEMP_SENSOR_ENDPOINT,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .attr_id = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
        .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
        .dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .u.send_info = {
            .min_interval = CONFIG_TP_REPORT_MIN_INTERVAL_S,
            .max_interval = CONFIG_TP_REPORT_MAX_INTERVAL_S,
            .def_min_interval = CONFIG_TP_REPORT_MIN_INTERVAL_S,
            .def_max_interval = CONFIG_TP_REPORT_MAX_INTERVAL_S,
            .delta.u16 = CONFIG_TP_REPORT_DELTA_CENTIDEG,
        },
    };
    esp_zb_zcl_update_reporting_info(&temp_info);

    /* Periodic heartbeat + change reporting for the Binary Input fault flag. */
    esp_zb_zcl_reporting_info_t fault_info = {
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .ep = HA_TEMP_SENSOR_ENDPOINT,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .attr_id = ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID,
        .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
        .dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .u.send_info = {
            .min_interval = 1,
            .max_interval = 300,
            .def_min_interval = 1,
            .def_max_interval = 300,
        },
    };
    esp_zb_zcl_update_reporting_info(&fault_info);
}

/* ------------------------------------------------------------------------- */
/* Endpoint assembly: base HA Temperature Sensor clusters + a Binary Input     */
/* fault flag + a manufacturer diagnostics cluster.                            */
/* ------------------------------------------------------------------------- */
static esp_zb_ep_list_t *build_endpoint(void)
{
    esp_zb_temperature_sensor_cfg_t sensor_cfg = ESP_ZB_DEFAULT_TEMPERATURE_SENSOR_CONFIG();
    sensor_cfg.temp_meas_cfg.min_value = TP_MEASURED_MIN_CENTIDEG;
    sensor_cfg.temp_meas_cfg.max_value = TP_MEASURED_MAX_CENTIDEG;
    /* Mains-powered Router: advertise it on the Basic cluster PowerSource attr
     * (otherwise it defaults to "unknown" and tools show no power source). */
    sensor_cfg.basic_cfg.power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_MAINS_SINGLE_PHASE;

    esp_zb_cluster_list_t *clusters = esp_zb_temperature_sensor_clusters_create(&sensor_cfg);

    /* Extra Basic-cluster identity attributes — these populate the device page
     * in Zigbee2MQTT / ZHA (hardware/app/stack version, build date, build id)
     * and make the auto-generated definition far more informative. */
    {
        esp_zb_attribute_list_t *basic =
            esp_zb_cluster_list_get_cluster(clusters, ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                                            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        uint8_t hw_version  = TP_HW_VERSION;
        uint8_t app_version = TP_APP_VERSION;
        uint8_t stack_version = TP_STACK_VERSION;
        esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_HW_VERSION_ID, &hw_version);
        esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &app_version);
        esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, &stack_version);
        /* ZCL strings are length-prefixed; the macros include the prefix byte. */
        char date_code[]   = ESP_TP_DATE_CODE;
        char sw_build_id[] = ESP_TP_SW_BUILD_ID;
        esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, date_code);
        esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, sw_build_id);
    }

    /* Binary Input (Basic): standard, interoperable RTD-fault indicator.
     * create() already declares OutOfService, PresentValue and StatusFlags. */
    esp_zb_binary_input_cluster_cfg_t bi_cfg = {
        .out_of_service = false,
        .status_flags = 0,
    };
    esp_zb_attribute_list_t *bi = esp_zb_binary_input_cluster_create(&bi_cfg);
    char bi_desc[] = "\x09" "RTD fault"; /* ZCL char string: length-prefixed */
    esp_zb_cluster_add_attr(bi, ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT,
                            ESP_ZB_ZCL_ATTR_BINARY_INPUT_DESCRIPTION_ID,
                            ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, bi_desc);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_binary_input_cluster(clusters, bi, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Manufacturer diagnostics cluster: detailed fault byte + capabilities. */
    esp_zb_attribute_list_t *diag = esp_zb_zcl_attr_list_create(TP_DIAG_CLUSTER_ID);
    esp_zb_custom_cluster_add_custom_attr(diag, TP_DIAG_ATTR_FAULT_RAW, ESP_ZB_ZCL_ATTR_TYPE_8BITMAP,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                          &s_diag_fault_raw);
    esp_zb_custom_cluster_add_custom_attr(diag, TP_DIAG_ATTR_RTD_WIRES, ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_diag_wires);
    esp_zb_custom_cluster_add_custom_attr(diag, TP_DIAG_ATTR_RTD_NOMINAL, ESP_ZB_ZCL_ATTR_TYPE_U16,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_diag_nominal);
    esp_zb_custom_cluster_add_custom_attr(diag, TP_DIAG_ATTR_RTD_REF, ESP_ZB_ZCL_ATTR_TYPE_U16,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_diag_ref);
    esp_zb_custom_cluster_add_custom_attr(diag, TP_DIAG_ATTR_FILTER_50HZ, ESP_ZB_ZCL_ATTR_TYPE_BOOL,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_diag_filter50);
    esp_zb_custom_cluster_add_custom_attr(diag, TP_DIAG_ATTR_SAMPLE_MS, ESP_ZB_ZCL_ATTR_TYPE_U16,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_diag_sample_ms);
    /* Writable: select antenna over Zigbee (1 = external U.FL, 0 = ceramic).
     * Seeded with the persisted/boot value so a read reflects reality. */
    esp_zb_custom_cluster_add_custom_attr(diag, TP_DIAG_ATTR_ANTENNA, ESP_ZB_ZCL_ATTR_TYPE_BOOL,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                          &s_diag_antenna_ext);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(clusters, diag, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = HA_TEMP_SENSOR_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg));
    return ep_list;
}

/* ------------------------------------------------------------------------- */
/* Commissioning helpers                                                       */
/* ------------------------------------------------------------------------- */
static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, ,
                        TAG, "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Deferred driver init %s",
                     deferred_driver_init() == ESP_OK ? "OK" : "FAILED");
            ESP_LOGI(TAG, "Device started in %s factory-reset mode",
                     esp_zb_bdb_is_factory_new() ? "" : "non-");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted, already on a network");
                s_joined = true;
            }
        } else {
            ESP_LOGW(TAG, "Failed to start Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t ext_pan_id;
            esp_zb_get_extended_pan_id(ext_pan_id);
            ESP_LOGI(TAG,
                     "Joined network (Ext PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, "
                     "PAN ID: 0x%04hx, Channel: %d, Short Addr: 0x%04hx)",
                     ext_pan_id[7], ext_pan_id[6], ext_pan_id[5], ext_pan_id[4],
                     ext_pan_id[3], ext_pan_id[2], ext_pan_id[1], ext_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            s_joined = true;
        } else {
            ESP_LOGI(TAG, "Network steering failed (status: %s), retrying", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Attribute write handler: a remote write to the antenna attribute switches    */
/* the RF path live and persists the choice.                                    */
/* ------------------------------------------------------------------------- */
static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty attribute message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG,
                        TAG, "Attribute write error status(%d)", message->info.status);

    if (message->info.dst_endpoint == HA_TEMP_SENSOR_ENDPOINT &&
        message->info.cluster == TP_DIAG_CLUSTER_ID &&
        message->attribute.id == TP_DIAG_ATTR_ANTENNA &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL &&
        message->attribute.data.value) {
        antenna_set(*(bool *)message->attribute.data.value);
    }
    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        return zb_attribute_handler((const esp_zb_zcl_set_attr_value_message_t *)message);
    default:
        ESP_LOGI(TAG, "Unhandled Zigbee action callback 0x%x", callback_id);
        return ESP_OK;
    }
}

/* ------------------------------------------------------------------------- */
/* Zigbee main task                                                            */
/* ------------------------------------------------------------------------- */
static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    esp_zb_ep_list_t *ep_list = build_endpoint();

    zcl_basic_manufacturer_info_t info = {
        .manufacturer_name = ESP_MANUFACTURER_NAME,
        .model_identifier = ESP_MODEL_IDENTIFIER,
    };
    esp_zcl_utility_add_ep_basic_manufacturer_info(ep_list, HA_TEMP_SENSOR_ENDPOINT, &info);

    esp_zb_device_register(ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    configure_reporting();

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Antenna routing must be set before the radio comes up. */
    configure_antenna();

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    xTaskCreate(esp_zb_task, "zb_main", 4096, NULL, 5, NULL);
}
