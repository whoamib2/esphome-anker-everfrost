# ESPHome Anker EverFrost

Experimental ESPHome external component for the Anker EverFrost 30 powered cooler.

## Current support

- Permanent BLE client connection
- Native ESPHome climate entity
- Current temperature
- Target-temperature control
- Target-temperature synchronization
- Battery percentage (provisional decode)
- Connection status
- Manual refresh button
- Optional raw BLE packet logging
- Standard ESPHome BLE RSSI sensor
- Bluetooth Proxy may remain enabled on the same ESP32

## Confirmed BLE protocol

Device name:

```text
POWEREDCOOLER_30
```

Known UUIDs:

```text
Service: 0x0156F5DA
Write characteristic: 0x7777
Notify characteristic: 0x8888
```

Checksum:

```text
sum(all preceding bytes) mod 256
```

Temperature encoding:

```text
encoded byte = whole degrees Fahrenheit + 128
```

Set-target command:

```text
08 EE 00 00 00 02 88 0B 00 TT CS
```

Full status packet:

- Byte 17: current temperature + 128
- Byte 18: target temperature + 128
- Byte 13: battery percentage (provisional; presently matches the cooler display)

## Installation

Place this repository on GitHub with the component under `components/everfrost`, then add:

```yaml
external_components:
  - source: github://whoamib2/esphome-anker-everfrost@main
    components: [everfrost]
```

See [`examples/everfrost-30.yaml`](examples/everfrost-30.yaml) for a complete configuration.

## Initial testing

For first boot, keep:

```yaml
raw_packet_logging: true
```

Confirm the log reports:

```text
Status: current=..., target=..., battery=80%
```

when the cooler itself shows 80%. After validation, change raw packet logging to `false`.

## Important

This is an early reverse-engineered component tested against an EverFrost 30. Other EverFrost models may use related but different packets or operating ranges.
