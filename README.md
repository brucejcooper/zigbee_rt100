# Stand-alone Zigbee temperature probe

ESP-IDF firmware for a **Seeed XIAO ESP32-C6** + **Adafruit MAX31865** RTD
amplifier. The board joins a Zigbee network as a mains-powered **Router** and
publishes the RTD temperature on the standard Home Automation *Temperature
Measurement* cluster, plus MAX31865 fault conditions and configuration.

## Hardware / wiring

SPI is wired per the [XIAO ESP32-C6 pinout](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/):

| Signal | XIAO pin | GPIO | MAX31865 |
|--------|----------|------|----------|
| SCK    | D8       | 19   | CLK      |
| MISO   | D9       | 20   | SDO      |
| MOSI   | D10      | 18   | SDI      |
| CS     | D7       | 17   | CS       |
| 3V3 / GND | —     | —    | VIN / GND |

**External antenna** (XIAO ESP32-C6 on-board RF switch):
- `GPIO3` driven **LOW** powers the RF switch.
- `GPIO14` driven **HIGH** routes RF to the external U.FL connector.

Both are configured in `app_main()` before the radio starts. Turn the external
antenna off (use the on-board ceramic) via `menuconfig` if you don't have one
fitted — leaving GPIO14 high with no antenna attached just reduces range.

All pins, the RTD type (PT100/PT1000), wiring (2/3/4-wire), reference resistor,
and mains-filter frequency are configurable under
**menuconfig → Temperature Probe (MAX31865 + Zigbee)**. Defaults match the
Adafruit **PT100** breakout (430 Ω Rref, 3-wire) with a **50 Hz** filter
(Australia/EU); switch the filter to 60 Hz in 60 Hz regions.

## Build / flash

This project needs the Zigbee ZCZR (Coordinator/Router) libraries, which are
pulled automatically as managed components on first configure.

```sh
. ~/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py menuconfig          # optional: adjust pins / RTD / reporting
idf.py -p <PORT> flash monitor
```

The target and Zigbee role are pre-set in `sdkconfig.defaults`
(`CONFIG_ZB_ZCZR=y`), so a plain `idf.py build` works out of the box.

## What it exposes (endpoint 10)

| Cluster | ID | Purpose |
|---------|----|---------|
| Basic | 0x0000 | Manufacturer / model identity |
| Identify | 0x0003 | Commissioning identify |
| Temperature Measurement | 0x0402 | `MeasuredValue` — temperature in 0.01 °C |
| Binary Input (Basic) | 0x000F | `PresentValue` = RTD fault active; `StatusFlags` FAULT bit |
| Diagnostics (mfr-specific) | 0xFC00 | Raw fault byte + sensor capabilities |

### Fault reporting

The MAX31865 fault-status register is read every sample cycle. On any
**change** of fault state the firmware:
- sets the Binary Input `PresentValue`/`StatusFlags` (interoperable — shows up
  in Zigbee2MQTT / Home Assistant as a fault flag), and
- writes the **raw fault byte** to diagnostics attribute `0xFC00/0x0000`
  (8-bit bitmap), then pushes an immediate Report Attributes command so the
  fault is delivered without waiting for the reporting interval.

Raw fault bits (per the MAX31865 datasheet): `0x80` RTD high threshold,
`0x40` RTD low threshold, `0x20` REFIN- > 0.85·Vbias, `0x10` REFIN- < 0.85·Vbias,
`0x08` RTDIN- < 0.85·Vbias, `0x04` over/under-voltage. The decoded fault is also
printed to the serial log.

The diagnostics cluster additionally reports static **capabilities**: RTD wire
count (`0x0010`), nominal ohms (`0x0011`), reference ohms (`0x0012`),
50 Hz-filter flag (`0x0013`) and the local sample period in ms (`0x0014`).

## Reporting rate — important

Yes, the **client configures the reporting rate** with the standard ZCL
*Configure Reporting* command (min interval, max interval, reportable change)
against the Temperature Measurement cluster. Defaults are set in `menuconfig`
(min 1 s, max 10 s, 0.2 °C change) and a coordinator can override them at
runtime.

**But standard Zigbee reporting intervals are whole seconds — the floor is one
report per second.** There is no sub-second standard reporting rate.

If you genuinely need **multiple reports per second**, enable the optional
**fast push-report** mode: *menuconfig → Sampling & reporting → Fast push-report
period (ms)*. Set e.g. `200` for 5 Hz. In this mode the firmware additionally
sends unsolicited Report Attributes commands at that period (to whatever is
bound to the cluster — typically the coordinator), bypassing the 1 s floor.
Leave it at `0` to rely solely on standard, client-configurable reporting.

Local sampling of the RTD (independent of reporting) runs at the **sample
period** (default 250 ms); the MAX31865 runs in continuous conversion mode so
every read returns a fresh value. Note that pushing many reports per second adds
meaningful Zigbee traffic and battery/airtime cost on the network — use it
deliberately.

## Zigbee2MQTT integration

Without a converter, Z2M shows the device as an "Automatically generated
definition" and the custom attributes are awkward to reach. The external
converter in `z2m/mechination_tempprobe.js` (Zigbee2MQTT 2.x) fixes that:

1. Copy `z2m/mechination_tempprobe.js` into the Z2M `external_converters/`
   folder (alongside `configuration.yaml`, i.e. `<data>/external_converters/`).
2. Restart Zigbee2MQTT.
3. On the device page, click **Reconfigure** (runs binding + reporting), then
   **Refresh** the read-only diagnostic values.

It exposes:
- **temperature** (°C)
- **rtd_fault** — OK / FAULT, pushed by the device on change
- **antenna_external** — writable: `external_ufl` / `onboard_ceramic`
  (persisted on the device)
- diagnostics: raw fault byte, RTD wires/nominal/reference, mains filter,
  sample period

## File layout

| File | Role |
|------|------|
| `main/temp_probe.c` | Zigbee Router app: endpoint, reporting, fault publishing |
| `main/temp_probe.h` | Zigbee config, cluster/attribute IDs |
| `main/max31865.c/.h` | MAX31865 SPI driver (continuous mode, CVD conversion) |
| `main/Kconfig.projbuild` | All tunable pins / RTD / reporting settings |
| `sdkconfig.defaults` | Target esp32c6, Zigbee ZCZR, partition table |
| `partitions.csv` | Custom table with Zigbee storage partitions |
