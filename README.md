# ESPHome Anker EverFrost

Experimental ESPHome external component for Anker EverFrost powered coolers.

## Current support

### EverFrost 30

- Permanent BLE client connection
- Native ESPHome climate entity
- Current temperature
- Target-temperature control and synchronization
- Battery percentage
- Connection status
- Manual refresh button
- Optional raw BLE packet logging

### EverFrost 50 dual-zone

- Automatic detection of the EverFrost 50 BLE service
- Separate ESPHome climate entities for Zone 1 and Zone 2
- Current temperature for both zones
- Target-temperature control and synchronization for both zones
- Zone enable command automatically sent before a target change, matching the official app behavior
- Battery percentage and live battery notifications
- Connection status
- Manual refresh button
- Optional raw BLE packet logging

A standard ESPHome BLE RSSI sensor can be used for either model, and Bluetooth Proxy may remain enabled on the same ESP32.

## Confirmed BLE protocol

### EverFrost 30

Device name:

```text
POWEREDCOOLER_30
```

```text
Service: 0x0156F5DA
Write characteristic: 0x7777
Notify characteristic: 0x8888
```

### EverFrost 50

Device name observed:

```text
POWEREDCOOLER_50
```

```text
Service: 0x0158F5DA
Write characteristic: 0x7777 (write without response)
Notify characteristic: 0x8888
```

Confirmed 50L commands/notifications:

```text
Zone 1 set target command: 0x83
Zone 2 set target command: 0x84
Zone 1 enable command:     0x86
Zone 2 enable command:     0x87

Zone 1 current notification: 0x06
Zone 2 current notification: 0x07
Zone 1 target notification:  0x0C
Zone 2 target notification:  0x0D
Battery notification:        0x04
```

Common checksum:

```text
sum(all preceding bytes) mod 256
```

Temperature encoding:

```text
encoded byte = whole degrees Fahrenheit + 128
```

Common startup/status request:

```text
08 EE 00 00 00 01 01 0A 00 02
```

EverFrost 30 set-target command:

```text
08 EE 00 00 00 02 88 0B 00 TT CS
```

EverFrost 30 full status packet currently decodes:

- Byte 17: current temperature + 128
- Byte 18: target temperature + 128
- Byte 13: battery percentage

## Installation

Add the external component:

```yaml
external_components:
  - source: github://whoamib2/esphome-anker-everfrost@main
    components: [everfrost]
```

For a single EverFrost 30, see [`examples/everfrost-30.yaml`](examples/everfrost-30.yaml).

For one ESP32 connected to both an EverFrost 30 and an EverFrost 50, see [`examples/everfrost-30-and-50.yaml`](examples/everfrost-30-and-50.yaml).

The EverFrost 50 climate configuration looks like:

```yaml
climate:
  - platform: everfrost
    id: everfrost_50
    name: "EverFrost 50 Zone 1"
    ble_client_id: everfrost_50_ble
    update_interval: 60s
    raw_packet_logging: true
    zone_2:
      id: everfrost_50_zone_2
      name: "EverFrost 50 Zone 2"
```

## Initial testing

For first boot after a protocol/component update, keep:

```yaml
raw_packet_logging: true
```

Confirm that the BLE model is detected, both zone temperatures synchronize on the EverFrost 50, and target changes from Home Assistant are reflected by the cooler. After validation, raw packet logging can be disabled.

## Status

This is an early reverse-engineered component. EverFrost 30 and EverFrost 50 behavior described above is based on direct BLE captures from tested hardware. Other EverFrost models may use related but different services, commands, status layouts, or operating ranges.
