// Zigbee2MQTT 2.x external converter for the Mechination "TempProbe-C6-RTD".
//
// XIAO ESP32-C6 + Adafruit MAX31865 PT100 probe. Exposes temperature, an RTD
// fault flag, the live antenna selection (writable), and read-only diagnostics
// describing the sensor configuration.
//
// Install: copy this file into the Zigbee2MQTT `external_converters/` folder
// (next to configuration.yaml, i.e. <data>/external_converters/), then restart
// Zigbee2MQTT. The device's "Automatically generated definition" badge should
// disappear and it should show as Mechination TempProbe-C6-RTD.

import {Zcl} from 'zigbee-herdsman';
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['TempProbe-C6-RTD'],
    model: 'TempProbe-C6-RTD',
    vendor: 'Mechination',
    description: 'PT100 RTD temperature probe (ESP32-C6 + MAX31865) with fault reporting',
    extend: [
        // Manufacturer diagnostics/control cluster 0xFC00. Attributes are plain
        // (non-manufacturer-specific), matching the firmware registration.
        m.deviceAddCustomCluster('mechinationDiag', {
            ID: 0xfc00,
            attributes: {
                faultRaw: {ID: 0x0000, type: Zcl.DataType.BITMAP8},
                rtdWires: {ID: 0x0010, type: Zcl.DataType.UINT8},
                rtdNominalOhm: {ID: 0x0011, type: Zcl.DataType.UINT16},
                rtdRefOhm: {ID: 0x0012, type: Zcl.DataType.UINT16},
                filter50hz: {ID: 0x0013, type: Zcl.DataType.BOOLEAN},
                samplePeriodMs: {ID: 0x0014, type: Zcl.DataType.UINT16},
                antenna: {ID: 0x0020, type: Zcl.DataType.BOOLEAN},
            },
            commands: {},
            commandsResponse: {},
        }),

        // Primary measurement.
        m.temperature(),

        // RTD fault flag — pushed by the device (standard Binary Input cluster).
        m.binary({
            name: 'rtd_fault',
            cluster: 'genBinaryInput',
            attribute: 'presentValue',
            valueOn: ['FAULT', 1],
            valueOff: ['OK', 0],
            description: 'RTD fault detected by the MAX31865 (open/short/out-of-range)',
            access: 'STATE_GET',
            reporting: {min: 0, max: 3600, change: 0},
            entityCategory: 'diagnostic',
        }),

        // Antenna selection — writable, persisted on the device.
        m.binary({
            name: 'antenna_external',
            cluster: 'mechinationDiag',
            attribute: 'antenna',
            valueOn: ['external_ufl', 1],
            valueOff: ['onboard_ceramic', 0],
            description: 'RF antenna: external U.FL vs on-board ceramic (persisted on device)',
            access: 'ALL',
            entityCategory: 'config',
        }),

        // Read-only diagnostics (refresh to read).
        m.numeric({
            name: 'rtd_fault_raw',
            cluster: 'mechinationDiag',
            attribute: 'faultRaw',
            access: 'STATE_GET',
            description: 'Raw MAX31865 fault byte: 0x80 RTD>hi, 0x40 RTD<lo, 0x20/0x10 REFIN, 0x08 RTDIN, 0x04 OV/UV',
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'rtd_wires',
            cluster: 'mechinationDiag',
            attribute: 'rtdWires',
            access: 'STATE_GET',
            description: 'RTD wiring configuration',
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'rtd_nominal',
            cluster: 'mechinationDiag',
            attribute: 'rtdNominalOhm',
            unit: 'Ω',
            access: 'STATE_GET',
            description: 'RTD nominal resistance at 0 °C (100 = PT100, 1000 = PT1000)',
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'rtd_reference',
            cluster: 'mechinationDiag',
            attribute: 'rtdRefOhm',
            unit: 'Ω',
            access: 'STATE_GET',
            description: 'Reference resistor (430 = PT100 board, 4300 = PT1000 board)',
            entityCategory: 'diagnostic',
        }),
        m.binary({
            name: 'mains_filter_50hz',
            cluster: 'mechinationDiag',
            attribute: 'filter50hz',
            valueOn: ['50Hz', 1],
            valueOff: ['60Hz', 0],
            access: 'STATE_GET',
            description: 'Mains rejection notch filter frequency',
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'sample_period',
            cluster: 'mechinationDiag',
            attribute: 'samplePeriodMs',
            unit: 'ms',
            access: 'STATE_GET',
            description: 'Local RTD sampling period',
            entityCategory: 'diagnostic',
        }),

        m.identify(),
    ],
    meta: {},
};
