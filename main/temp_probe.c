/*
 * WiFi/MQTT temperature probe for the Seeed XIAO ESP32-C6 with an Adafruit
 * MAX31865 RTD amplifier on the standard SPI pins.
 *
 * - Joins a 2.4 GHz WiFi network (station) and connects to an MQTT broker.
 * - Samples the RTD with the MAX31865 in continuous mode; DRDY drives an
 *   exponential moving average over every conversion (~50/s). The smoothed
 *   value is published to MQTT at TP_SAMPLE_PERIOD_MS (>= 1 Hz).
 * - Publishes Home Assistant MQTT-discovery configs for every entity so they
 *   appear automatically: temperature, RTD-fault flag, the raw fault byte and
 *   the static sensor configuration (diagnostic), plus two writable config
 *   entities -- the EMA smoothing alpha (number) and the antenna select (switch).
 * - On an RTD fault (or no trustworthy sample) the temperature entity is marked
 *   unavailable via its own availability topic rather than reporting a bogus
 *   number. A broker-side Last Will marks the whole device offline if it drops.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"

#include "temp_probe.h"
#include "max31865.h"

static const char *TAG = "TEMP_PROBE";

static max31865_handle_t s_rtd;

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

/* ----- Persisted runtime config ------------------------------------------ */
#define TP_NVS_NAMESPACE "tempprobe"
#define TP_NVS_KEY_ANT   "ant_ext"
#define TP_NVS_KEY_EMA   "ema_a"

#if CONFIG_TP_USE_EXTERNAL_ANTENNA
#define TP_ANTENNA_DEFAULT true
#else
#define TP_ANTENNA_DEFAULT false
#endif

static bool     s_antenna_ext   = TP_ANTENNA_DEFAULT;
static uint16_t s_ema_permille  = CONFIG_TP_EMA_ALPHA_PERMILLE; /* alpha x 1000 */
static float    s_ema_alpha     = CONFIG_TP_EMA_ALPHA_PERMILLE / 1000.0f;

/* ----- MQTT client + connection state ------------------------------------ */
static esp_mqtt_client_handle_t s_mqtt;
static volatile bool s_mqtt_connected = false;
/* Set on every (re)connect so the sensor task republishes its live state once,
 * since retained edges may have been missed while disconnected. */
static volatile bool s_republish = false;

static TaskHandle_t s_sensor_task;

/* ----- Topic strings (built once: <prefix>/<device_id>/...) --------------- */
static char s_node_id[32];       /* device id (Kconfig or MAC-derived)   */
static char t_status[96];        /* device availability (LWT)            */
static char t_temp[96];          /* temperature value                    */
static char t_temp_status[96];   /* temperature availability             */
static char t_fault[96];         /* RTD fault flag ON/OFF                 */
static char t_fault_raw[96];     /* raw fault byte                       */
static char t_wires[96];
static char t_nominal[96];
static char t_ref[96];
static char t_filter[96];
static char t_alpha[96];
static char t_alpha_set[96];
static char t_ant[96];
static char t_ant_set[96];

/* ------------------------------------------------------------------------- */
/* NVS-persisted settings                                                      */
/* ------------------------------------------------------------------------- */
static uint16_t ema_clamp(uint16_t permille)
{
    if (permille < 1)    return 1;
    if (permille > 1000) return 1000;
    return permille;
}

static void prefs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(TP_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint8_t ant = 0;
        if (nvs_get_u8(h, TP_NVS_KEY_ANT, &ant) == ESP_OK) {
            s_antenna_ext = (ant != 0);
        }
        uint16_t ema = 0;
        if (nvs_get_u16(h, TP_NVS_KEY_EMA, &ema) == ESP_OK) {
            s_ema_permille = ema;
        }
        nvs_close(h);
    }
    s_ema_permille = ema_clamp(s_ema_permille);
    s_ema_alpha = s_ema_permille / 1000.0f;
}

static void prefs_save_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(TP_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void prefs_save_u16(const char *key, uint16_t v)
{
    nvs_handle_t h;
    if (nvs_open(TP_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u16(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ------------------------------------------------------------------------- */
/* Antenna control: GPIO3 (active LOW) powers the RF switch, GPIO14 selects     */
/* the antenna (LOW = on-board ceramic, HIGH = external U.FL).                  */
/* ------------------------------------------------------------------------- */
static void antenna_apply(bool external)
{
    gpio_set_level(CONFIG_TP_ANT_RF_SWITCH_EN_GPIO, 0);
    gpio_set_level(CONFIG_TP_ANT_SELECT_GPIO, external ? 1 : 0);
    s_antenna_ext = external;
}

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
    antenna_apply(s_antenna_ext);
    ESP_LOGI(TAG, "Antenna: %s", s_antenna_ext ? "external U.FL" : "on-board ceramic");
}

/* ------------------------------------------------------------------------- */
/* MQTT publish helper                                                         */
/* ------------------------------------------------------------------------- */
static void pub(const char *topic, const char *payload, bool retain)
{
    if (!s_mqtt_connected) {
        return;
    }
    esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, retain ? 1 : 0);
}

/* ------------------------------------------------------------------------- */
/* Home Assistant MQTT discovery                                               */
/* ------------------------------------------------------------------------- */
/* UTF-8 unit strings, kept as separate literals so the \xNN hex escapes are
 * bounded (the C tokenizer would otherwise swallow following chars as hex). */
#define DEG_C "\xC2\xB0" "C"   /* °C */
#define OHM   "\xCE\xA9"       /* Ω  */

static char s_device_json[320];

static void build_device_json(void)
{
    snprintf(s_device_json, sizeof(s_device_json),
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
             "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}",
             s_node_id, CONFIG_TP_DEVICE_NAME, TP_MANUFACTURER, TP_MODEL, TP_SW_VERSION);
}

/* Wrap an entity's JSON members with availability + device + unique_id and
 * publish the retained discovery config. `fields` is the comma-separated
 * entity-specific members (no surrounding braces). */
static void disco(const char *component, const char *object, const char *fields, bool temp_avail)
{
    char avail[320];
    if (temp_avail) {
        snprintf(avail, sizeof(avail),
                 "\"availability\":[{\"topic\":\"%s\"},{\"topic\":\"%s\"}],"
                 "\"availability_mode\":\"all\"", t_status, t_temp_status);
    } else {
        snprintf(avail, sizeof(avail), "\"availability\":[{\"topic\":\"%s\"}]", t_status);
    }

    char payload[1500];
    snprintf(payload, sizeof(payload), "{%s,%s,%s,\"unique_id\":\"%s_%s\"}",
             fields, avail, s_device_json, s_node_id, object);

    char topic[224];
    snprintf(topic, sizeof(topic), "%s/%s/%s/%s/config",
             CONFIG_TP_HA_DISCOVERY_PREFIX, component, s_node_id, object);
    pub(topic, payload, true);
}

static void publish_discovery(void)
{
    char f[512];

    /* Temperature (unavailable on fault via its own status topic). */
    snprintf(f, sizeof(f),
             "\"name\":\"Temperature\",\"device_class\":\"temperature\","
             "\"state_class\":\"measurement\",\"unit_of_measurement\":\"" DEG_C "\","
             "\"suggested_display_precision\":2,\"state_topic\":\"%s\"", t_temp);
    disco("sensor", "temperature", f, true);

    /* RTD fault flag. */
    snprintf(f, sizeof(f),
             "\"name\":\"RTD fault\",\"device_class\":\"problem\","
             "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
             "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\"", t_fault);
    disco("binary_sensor", "rtd_fault", f, false);

    /* Raw fault byte. */
    snprintf(f, sizeof(f),
             "\"name\":\"RTD fault byte\",\"icon\":\"mdi:alert-circle-outline\","
             "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\"", t_fault_raw);
    disco("sensor", "rtd_fault_raw", f, false);

    /* Static sensor configuration (diagnostic). */
    snprintf(f, sizeof(f),
             "\"name\":\"RTD wires\",\"entity_category\":\"diagnostic\","
             "\"state_topic\":\"%s\"", t_wires);
    disco("sensor", "rtd_wires", f, false);

    snprintf(f, sizeof(f),
             "\"name\":\"RTD nominal\",\"unit_of_measurement\":\"" OHM "\","
             "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\"", t_nominal);
    disco("sensor", "rtd_nominal", f, false);

    snprintf(f, sizeof(f),
             "\"name\":\"RTD reference\",\"unit_of_measurement\":\"" OHM "\","
             "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\"", t_ref);
    disco("sensor", "rtd_reference", f, false);

    snprintf(f, sizeof(f),
             "\"name\":\"Mains filter\",\"unit_of_measurement\":\"Hz\","
             "\"entity_category\":\"diagnostic\",\"state_topic\":\"%s\"", t_filter);
    disco("sensor", "mains_filter", f, false);

    /* Writable: antenna select (switch). */
    snprintf(f, sizeof(f),
             "\"name\":\"External antenna\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
             "\"icon\":\"mdi:antenna\",\"entity_category\":\"config\","
             "\"state_topic\":\"%s\",\"command_topic\":\"%s\"", t_ant, t_ant_set);
    disco("switch", "antenna", f, false);

    /* Writable: EMA smoothing alpha (number). */
    snprintf(f, sizeof(f),
             "\"name\":\"Smoothing alpha\",\"min\":0.01,\"max\":1,\"step\":0.01,"
             "\"mode\":\"box\",\"icon\":\"mdi:chart-bell-curve\","
             "\"entity_category\":\"config\","
             "\"state_topic\":\"%s\",\"command_topic\":\"%s\"", t_alpha, t_alpha_set);
    disco("number", "alpha", f, false);
}

/* ------------------------------------------------------------------------- */
/* State publishing                                                            */
/* ------------------------------------------------------------------------- */
static void publish_config_state(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", s_ema_alpha);
    pub(t_alpha, buf, true);
    pub(t_ant, s_antenna_ext ? "ON" : "OFF", true);
}

static void publish_static_diag(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", TP_WIRE_COUNT);
    pub(t_wires, buf, true);
    snprintf(buf, sizeof(buf), "%d", CONFIG_TP_RTD_NOMINAL_OHMS);
    pub(t_nominal, buf, true);
    snprintf(buf, sizeof(buf), "%d", CONFIG_TP_RTD_REF_OHMS);
    pub(t_ref, buf, true);
    pub(t_filter, CONFIG_TP_RTD_FILTER_50HZ ? "50" : "60", true);
}

/* ------------------------------------------------------------------------- */
/* MQTT command handling                                                       */
/* ------------------------------------------------------------------------- */
static void handle_command(const char *topic, int tlen, const char *data, int dlen)
{
    char payload[32];
    int n = dlen < (int)sizeof(payload) - 1 ? dlen : (int)sizeof(payload) - 1;
    memcpy(payload, data, n);
    payload[n] = '\0';

    if (tlen == (int)strlen(t_alpha_set) && strncmp(topic, t_alpha_set, tlen) == 0) {
        float a = strtof(payload, NULL);
        uint16_t permille = ema_clamp((uint16_t)lroundf(a * 1000.0f));
        s_ema_permille = permille;
        s_ema_alpha = permille / 1000.0f;
        prefs_save_u16(TP_NVS_KEY_EMA, permille);
        ESP_LOGW(TAG, "alpha set to %.3f via MQTT (persisted)", s_ema_alpha);
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", s_ema_alpha);
        pub(t_alpha, buf, true);
    } else if (tlen == (int)strlen(t_ant_set) && strncmp(topic, t_ant_set, tlen) == 0) {
        bool ext = (strncmp(payload, "ON", 2) == 0) || (payload[0] == '1');
        antenna_apply(ext);
        prefs_save_u8(TP_NVS_KEY_ANT, ext ? 1 : 0);
        ESP_LOGW(TAG, "antenna set to %s via MQTT (persisted)",
                 ext ? "external U.FL" : "on-board ceramic");
        pub(t_ant, ext ? "ON" : "OFF", true);
    }
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_mqtt_connected = true;
        esp_mqtt_client_subscribe(s_mqtt, t_alpha_set, 1);
        esp_mqtt_client_subscribe(s_mqtt, t_ant_set, 1);
        pub(t_status, "online", true);      /* clears the Last Will */
        publish_discovery();
        publish_static_diag();
        publish_config_state();
        s_republish = true;                 /* let the sensor task re-emit live state */
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_mqtt_connected = false;
        break;
    case MQTT_EVENT_DATA:
        handle_command(event->topic, event->topic_len, event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    if (s_mqtt) {
        return;
    }
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_TP_MQTT_URI,
        .credentials.username = CONFIG_TP_MQTT_USER,
        .credentials.authentication.password = CONFIG_TP_MQTT_PASSWORD,
        /* Last Will: the broker publishes "offline" (retained) to the device
         * availability topic if this client drops without a clean disconnect
         * (power loss, crash, WiFi loss), marking every HA entity unavailable.
         * A short keepalive makes that detection prompt (~1.5x = ~22 s) instead
         * of the 120 s default. On (re)connect we publish "online" ourselves. */
        .session.keepalive = 15,
        .session.last_will = {
            .topic = t_status,
            .msg = "offline",
            .msg_len = 0,
            .qos = 1,
            .retain = 1,
        },
    };
    s_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
}

/* ------------------------------------------------------------------------- */
/* WiFi station                                                                */
/* ------------------------------------------------------------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = data;
        s_mqtt_connected = false;
        ESP_LOGW(TAG, "WiFi disconnected (reason=%d), reconnecting", e ? e->reason : -1);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        mqtt_start();
    }
}

static void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler, NULL, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, CONFIG_TP_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, CONFIG_TP_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = strlen(CONFIG_TP_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK
                                                                : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    /* AU regulatory domain (channels 1-13). Antenna was already selected in
     * configure_antenna() before WiFi init, so RF calibration ran on the right
     * path -- selecting it afterwards would calibrate against a dead antenna. */
    esp_wifi_set_country_code("AU", false);
    ESP_LOGI(TAG, "WiFi started (ant=%s); joining \"%s\"",
             s_antenna_ext ? "external U.FL" : "on-board ceramic", CONFIG_TP_WIFI_SSID);
    esp_wifi_connect();
}

/* ------------------------------------------------------------------------- */
/* Sensor sampling task: DRDY-driven EMA, publish to MQTT each period.          */
/* ------------------------------------------------------------------------- */
#if CONFIG_TP_DRDY_GPIO >= 0
static void IRAM_ATTR drdy_isr(void *arg)
{
    BaseType_t hpw = pdFALSE;
    vTaskNotifyGiveFromISR(s_sensor_task, &hpw);
    portYIELD_FROM_ISR(hpw);
}
#endif

static void sensor_task(void *arg)
{
    const int64_t window_us = (int64_t)CONFIG_TP_SAMPLE_PERIOD_MS * 1000;
    s_sensor_task = xTaskGetCurrentTaskHandle();

#if CONFIG_TP_DRDY_GPIO >= 0
    gpio_config_t drdy = {
        .pin_bit_mask = 1ULL << CONFIG_TP_DRDY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&drdy));
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(isr_err);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_TP_DRDY_GPIO, drdy_isr, NULL));
#endif

    double ema = 0.0;
    bool ema_valid = false;
    uint32_t accepted = 0;
    int64_t last_emit = esp_timer_get_time();

    bool prev_valid = false;
    bool prev_fault_active = false;
    uint8_t prev_fault_raw = 0xFF; /* force first publish */
    bool first = true;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONFIG_TP_SAMPLE_PERIOD_MS));

        float temp_c = 0.0f;
        if (max31865_read_temperature(s_rtd, &temp_c) == ESP_OK &&
            temp_c >= TP_RTD_VALID_MIN_C && temp_c <= TP_RTD_VALID_MAX_C) {
            if (!ema_valid) {
                ema = temp_c;
                ema_valid = true;
            } else {
                ema += (double)s_ema_alpha * ((double)temp_c - ema);
            }
            accepted++;
        }

        if (esp_timer_get_time() - last_emit < window_us) {
            continue;   /* keep integrating until the publish period elapses */
        }
        last_emit = esp_timer_get_time();

        uint8_t fault_raw = 0;
        max31865_read_fault(s_rtd, &fault_raw);
        bool fault_active = (fault_raw != 0);
        bool valid = (!fault_active && accepted > 0);

        if (s_republish) {
            /* Re-emit edge-triggered state after a reconnect. */
            first = true;
            s_republish = false;
        }

        if (s_mqtt_connected) {
            if (valid) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.2f", ema);
                pub(t_temp, buf, true);
            } else {
                ema_valid = false; /* re-seed cleanly on recovery */
            }
            if (first || valid != prev_valid) {
                pub(t_temp_status, valid ? "online" : "offline", true);
                prev_valid = valid;
            }
            if (first || fault_active != prev_fault_active) {
                pub(t_fault, fault_active ? "ON" : "OFF", true);
                prev_fault_active = fault_active;
            }
            if (first || fault_raw != prev_fault_raw) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%u", fault_raw);
                pub(t_fault_raw, buf, true);
                prev_fault_raw = fault_raw;
            }
            first = false;
        }

        if (valid) {
            ESP_LOGD(TAG, "EMA=%.3f C alpha=%.3f", ema, s_ema_alpha);
        } else {
            ESP_LOGW(TAG, "RTD unavailable (fault=0x%02x, in-range samples=%u)",
                     fault_raw, (unsigned)accepted);
        }

        if (fault_active) {
            max31865_clear_fault(s_rtd);
        }
        accepted = 0;
    }
}

/* ------------------------------------------------------------------------- */
/* Factory-reset pushbutton: hold to erase persisted runtime config + reboot.  */
/* ------------------------------------------------------------------------- */
static void factory_reset_button_task(void *arg)
{
    const gpio_num_t pin = CONFIG_TP_FACTORY_RESET_GPIO;
    const int poll_ms = 50;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    int held_ms = 0;
    bool announced = false;
    while (1) {
        if (gpio_get_level(pin) == 0) {
            held_ms += poll_ms;
            if (held_ms >= CONFIG_TP_FACTORY_RESET_HOLD_MS && !announced) {
                announced = true;
                ESP_LOGW(TAG, "Factory-reset threshold reached; release to erase & reboot");
            }
        } else {
            if (held_ms >= CONFIG_TP_FACTORY_RESET_HOLD_MS) {
                ESP_LOGW(TAG, "Factory reset: erasing persisted config and rebooting");
                nvs_handle_t h;
                if (nvs_open(TP_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
                    nvs_erase_all(h);
                    nvs_commit(h);
                    nvs_close(h);
                }
                esp_restart();
            }
            held_ms = 0;
            announced = false;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
    }
}

/* ------------------------------------------------------------------------- */
static void build_topics(void)
{
    if (strlen(CONFIG_TP_DEVICE_ID) > 0) {
        snprintf(s_node_id, sizeof(s_node_id), "%s", CONFIG_TP_DEVICE_ID);
    } else {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_node_id, sizeof(s_node_id), "tempprobe_%02x%02x%02x", mac[3], mac[4], mac[5]);
    }

    char base[64];
    snprintf(base, sizeof(base), "%s/%s", CONFIG_TP_MQTT_TOPIC_PREFIX, s_node_id);
    snprintf(t_status,      sizeof(t_status),      "%s/status", base);
    snprintf(t_temp,        sizeof(t_temp),        "%s/temperature", base);
    snprintf(t_temp_status, sizeof(t_temp_status), "%s/temperature/status", base);
    snprintf(t_fault,       sizeof(t_fault),       "%s/fault", base);
    snprintf(t_fault_raw,   sizeof(t_fault_raw),   "%s/fault_raw", base);
    snprintf(t_wires,       sizeof(t_wires),       "%s/rtd_wires", base);
    snprintf(t_nominal,     sizeof(t_nominal),     "%s/rtd_nominal", base);
    snprintf(t_ref,         sizeof(t_ref),         "%s/rtd_ref", base);
    snprintf(t_filter,      sizeof(t_filter),      "%s/filter_hz", base);
    snprintf(t_alpha,       sizeof(t_alpha),       "%s/alpha", base);
    snprintf(t_alpha_set,   sizeof(t_alpha_set),   "%s/alpha/set", base);
    snprintf(t_ant,         sizeof(t_ant),         "%s/antenna", base);
    snprintf(t_ant_set,     sizeof(t_ant_set),     "%s/antenna/set", base);

    build_device_json();
}

static esp_err_t rtd_init(void)
{
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
    return max31865_init(&cfg, &s_rtd);
}

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    prefs_load();
    configure_antenna();
    build_topics();
    ESP_LOGI(TAG, "Node id: %s", s_node_id);

    ESP_ERROR_CHECK(rtd_init());
    xTaskCreate(sensor_task, "rtd_sample", 4096, NULL, 5, NULL);
    xTaskCreate(factory_reset_button_task, "fct_btn", 2560, NULL, 4, NULL);

    wifi_start();
}
