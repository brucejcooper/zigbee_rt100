/*
 * Zigbee Router temperature probe (XIAO ESP32-C6 + Adafruit MAX31865).
 */
#pragma once

#include "esp_zigbee_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zigbee config -------------------------------------------------------------*/
#define INSTALLCODE_POLICY_ENABLE   false   /* install-code commissioning off */
#define MAX_CHILDREN                10      /* router: max direct children */
#define HA_TEMP_SENSOR_ENDPOINT     10      /* endpoint hosting the temp cluster */
#define ESP_ZB_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

/* Basic-cluster identity (length-prefixed ZCL strings — the leading \xNN byte
 * is the string length and must match the text). manufacturer_name + model
 * identifier are the keys a Zigbee2MQTT/ZHA external converter matches on. */
#define ESP_MANUFACTURER_NAME "\x0b""Mechination"
#define ESP_MODEL_IDENTIFIER  "\x10""TempProbe-C6-RTD"
#define ESP_TP_DATE_CODE      "\x08""20260620"          /* YYYYMMDD build date */
#define ESP_TP_SW_BUILD_ID    "\x0c""1.0.0-c6-rtd"       /* firmware build id   */

/* Numeric Basic-cluster identity attributes. */
#define TP_HW_VERSION    1  /* XIAO ESP32-C6 + Adafruit MAX31865 rev 1 */
#define TP_APP_VERSION   1
#define TP_STACK_VERSION 1

/* Temperature MeasuredValue is an int16 in units of 0.01 C, so the cluster can
 * only represent up to +327.67 C regardless of the RTD's physical range. */
#define TP_MEASURED_MIN_CENTIDEG (-20000) /* -200.00 C */
#define TP_MEASURED_MAX_CENTIDEG (32767)  /* +327.67 C (int16 max) */

/* ZCL "invalid/unknown" MeasuredValue sentinel. Published on an RTD fault so the
 * coordinator surfaces the reading as unavailable instead of a bogus number. */
#define TP_MEASURED_INVALID      ((int16_t)0x8000)

/* Physically plausible PT100/PT1000 span. Readings outside this (e.g. the ~-242 C
 * an open circuit yields) are treated as invalid and never fed to the filter. */
#define TP_RTD_VALID_MIN_C       (-200.0f)
#define TP_RTD_VALID_MAX_C       (850.0f)

/* ----- Fault & capability reporting --------------------------------------- *
 * A standard Binary Input (Basic) cluster carries an interoperable "RTD fault"
 * flag (PresentValue=1 when faulted, with the FAULT StatusFlag bit set) so
 * tools like Zigbee2MQTT/Home Assistant surface it without custom handling.
 *
 * A manufacturer-specific diagnostics cluster carries the *detailed* MAX31865
 * fault-status byte plus the static sensor configuration (capabilities). */
#define TP_BI_STATUS_FLAG_FAULT  0x02 /* StatusFlags bit 1 = FAULT (ZCL) */

#define TP_MANUF_CODE            0x1037 /* manufacturer code for custom attrs */
#define TP_DIAG_CLUSTER_ID       0xFC00 /* manufacturer-specific cluster */
#define TP_DIAG_ATTR_FAULT_RAW   0x0000 /* 8-bit bitmap: raw fault-status reg  */
#define TP_DIAG_ATTR_RTD_WIRES   0x0010 /* u8:  2 / 3 / 4                       */
#define TP_DIAG_ATTR_RTD_NOMINAL 0x0011 /* u16: nominal RTD ohms (100/1000)     */
#define TP_DIAG_ATTR_RTD_REF     0x0012 /* u16: reference resistor ohms         */
#define TP_DIAG_ATTR_FILTER_50HZ 0x0013 /* bool: mains notch filter is 50 Hz    */
#define TP_DIAG_ATTR_ANTENNA     0x0020 /* bool R/W: 1=external U.FL, 0=ceramic  */
#define TP_DIAG_ATTR_EMA_ALPHA   0x0021 /* u16 R/W: EMA smoothing alpha x1000    */

/* Mains-powered Router role configuration. */
#define ESP_ZB_ZR_CONFIG()                              \
    {                                                   \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,       \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
        .nwk_cfg.zczr_cfg = {                           \
            .max_children = MAX_CHILDREN,               \
        },                                              \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()       \
    {                                       \
        .radio_mode = ZB_RADIO_MODE_NATIVE, \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                          \
    {                                                         \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, \
    }

#ifdef __cplusplus
}
#endif
