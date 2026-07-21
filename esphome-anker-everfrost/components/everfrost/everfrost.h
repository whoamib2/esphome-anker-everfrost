#pragma once

#include <cstdint>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/button/button.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>
#endif

namespace esphome {
namespace everfrost {

namespace espbt = esphome::esp32_ble_tracker;

class EverFrostClimate : public climate::Climate,
                         public PollingComponent,
                         public ble_client::BLEClientNode {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

#ifdef USE_ESP32
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
#endif

  void set_current_temperature_sensor(sensor::Sensor *sensor) {
    this->current_temperature_sensor_ = sensor;
  }
  void set_battery_sensor(sensor::Sensor *sensor) { this->battery_sensor_ = sensor; }
  void set_connected_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->connected_binary_sensor_ = sensor;
  }
  void set_raw_packet_logging(bool enabled) { this->raw_packet_logging_ = enabled; }

  void request_status();

 protected:
  void parse_packet_(const uint8_t *data, uint16_t length);
  bool validate_checksum_(const uint8_t *data, uint16_t length) const;
  uint8_t checksum_(const uint8_t *data, uint16_t length) const;
  void write_packet_(const std::vector<uint8_t> &packet);
  void send_startup_request_();
  void send_target_temperature_(float temperature_c);
  void publish_current_temperature_(float temperature_c);
  void publish_target_temperature_(float temperature_c);
  void publish_battery_(uint8_t battery);
  void log_packet_(const char *prefix, const uint8_t *data, uint16_t length) const;

  // EverFrost uses a 32-bit service UUID and 16-bit characteristic UUIDs.
  static constexpr uint32_t SERVICE_UUID = 0x0156F5DA;
  static constexpr uint16_t WRITE_CHAR_UUID = 0x7777;
  static constexpr uint16_t NOTIFY_CHAR_UUID = 0x8888;

  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};
  bool ready_{false};
  bool raw_packet_logging_{false};
  int pending_target_f_{-999};

  sensor::Sensor *current_temperature_sensor_{nullptr};
  sensor::Sensor *battery_sensor_{nullptr};
  binary_sensor::BinarySensor *connected_binary_sensor_{nullptr};
};

class EverFrostRefreshButton : public button::Button {
 public:
  void set_parent(EverFrostClimate *parent) { this->parent_ = parent; }

 protected:
  void press_action() override {
    if (this->parent_ != nullptr)
      this->parent_->request_status();
  }

  EverFrostClimate *parent_{nullptr};
};

}  // namespace everfrost
}  // namespace esphome
