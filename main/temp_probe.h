/*
 * WiFi/MQTT temperature probe (XIAO ESP32-C6 + Adafruit MAX31865).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device identity (reported in the Home Assistant discovery `device` block). */
#define TP_MANUFACTURER  "Mechination"
#define TP_MODEL         "TempProbe-C6-RTD"
#define TP_SW_VERSION    "2.0.0-c6-wifi"

/* Physically plausible PT100/PT1000 span. Readings outside this (e.g. the
 * ~-242 C an open circuit yields) are treated as a fault and never fed to the
 * EMA, so a garbage transition sample can't poison the filter. */
#define TP_RTD_VALID_MIN_C  (-200.0f)
#define TP_RTD_VALID_MAX_C  (850.0f)

#ifdef __cplusplus
}
#endif
