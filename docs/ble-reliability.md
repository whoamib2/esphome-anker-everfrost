# BLE reliability test update (2026-09-28.1)

This branch is based on main commit `7401df4098cbc617e4cfc9b222aa5893081b3e3a`. Main is not changed by this test update.

## Behavior

- Use the ESPHome BLE client's tracked `register_for_notify()` method.
- Hold the component node below ESTABLISHED until the exact notification descriptor write is acknowledged successfully. The ESPHome parent writes the descriptor; this component does not send a duplicate descriptor write.
- Check service discovery, notification registration, and descriptor-write status. A 20-second setup deadline initiates recovery if setup stalls.
- Request initial status 0.5 seconds after descriptor acknowledgement. Retry at 3 and 8 seconds only while the first complete valid status is missing.
- Initiate BLE-session recovery after 20 seconds without initial full status. Delay disconnection by 1, 2, 4, 8, 16, then at most 30 seconds on consecutive failures. A valid full status resets this backoff.
- Set the existing Connected binary sensor true only after a valid full status packet arrives, not merely when the BLE link opens.
- With a 60-second update interval, initiate recovery after 185 seconds without a complete valid status. RSSI and command acknowledgements do not reset this timer.
- Cancel session timers on disconnect and protect delayed recovery against a new session. Each cooler has separate recovery state.
- Mark temperatures and battery unknown when a session is reset instead of retaining stale measurements as live data.
- Recovery disconnects only the affected cooler's BLE session. It does not power-cycle the cooler, change its temperatures/settings, reboot the ESP32, or restart the shared BLE stack, Wi-Fi, or WireGuard. Reconnection still depends on the configured auto_connect client discovering an available peripheral.
- Preserve the existing temperature, zone-power, brightness, and voltage-protection command paths. The 30L remains Cool-only.
- Add the peer address to packet logs and print `BLE recovery revision: 2026-09-28.1` in the configuration dump.

The implementation remains in the existing everfrost.cpp/everfrost.h files. There are no implementation .inc files or globally included helper headers.

## Install in an existing configuration

Use ESPHome 2026.9.0 for the first hardware test. Replace only the existing external component reference:

```yaml
external_components:
  - source: github://whoamib2/esphome-anker-everfrost@ble-reliability-20260928
    components: [everfrost]
    refresh: 5min
```

Retain existing entity IDs/names, network settings, API encryption and OTA credentials. Keep raw_packet_logging enabled for the first test. The custom on_ble_advertise lambda that prints every nearby device can be removed without disabling the tracker or proxy.

The branch has different cached source from main. Validate, compile and install the firmware normally; this repository update does not flash a device by itself. Check the recovery revision in the live log after installation.

## Validation and limits

The actual updated C++ component was compiled on a host using deterministic ESPHome/ESP-IDF API mocks, g++ C++17, warnings-as-errors, and address/undefined-behavior sanitizers. Twenty scenarios passed: descriptor readiness, retry timing, timeout recovery, cancelling retries after valid status, setup failures, stalled status updates, independent coolers, cancelled old-session recovery, malformed frames, and preserved 30/50 protocol behavior.

This is NOT a full ESP32/ESP-IDF firmware compilation or an on-hardware Bluetooth test. Both remain necessary. The available log does not establish the exact original failure during notification setup, so these changes harden setup and recovery rather than claim a proven root-cause fix. There is no guarantee of a fixed reconnection time for an out-of-range, powered-off, or otherwise unavailable cooler.

## Rollback

Restore `@main` (or the base commit above) in external_components, then rebuild and reinstall. Keep a copy of the last working device YAML and firmware before testing.
