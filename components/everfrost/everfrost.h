#pragma once

#include <cstdint>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/button/button.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>
#endif

namespace esphome {
namespace everfrost {

namespace espbt = esphome::esp32_ble_tracker;

class EverFrostClimate;

class EverFrostZoneClimate : public climate::Climate {
 public:
  void set_parent(EverFrostClimate *parent) { this->parent_ = parent; }

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void publish_current_temperature_value(float temperature_c);
  void publish_target_temperature_value(float temperature_c);
  void publish_power_value(bool enabled);
  void publish_disconnected();

 protected:
  EverFrostClimate *parent_{nullptr};
};

// Legacy select classes retained for ABI/source compatibility with the
// monolithic implementation. New configurations use the direct classes below,
// which send the exact no-response writes captured from both the 30L and 50L.
class EverFrostVoltageProtectionSelect : public select::Select {
 public:
  void set_parent(EverFrostClimate *parent) { this->parent_ = parent; }

 protected:
  void control(size_t index) override;
  EverFrostClimate *parent_{nullptr};
};

class EverFrostScreenBrightnessSelect : public select::Select {
 public:
  void set_parent(EverFrostClimate *parent) { this->parent_ = parent; }

 protected:
  void control(size_t index) override;
  EverFrostClimate *parent_{nullptr};
};

class EverFrostClimate : public climate::Climate,
                         public PollingComponent,
                         public ble_client::BLEClientNode {
 public:
  enum Model : uint8_t {
    MODEL_UNKNOWN = 0,
    MODEL_30 = 30,
    MODEL_50 = 50,
  };

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
  void set_zone2_climate(EverFrostZoneClimate *climate) { this->zone2_climate_ = climate; }
  void set_voltage_protection_select(select::Select *select) {
    this->voltage_protection_select_ = select;
  }
  void set_screen_brightness_select(select::Select *select) {
    this->screen_brightness_select_ = select;
  }

  void request_status();
  void set_zone_target_temperature(uint8_t zone, float temperature_c);
  void set_zone_power(uint8_t zone, bool enabled);
  void set_voltage_protection(uint8_t level);
  void set_screen_brightness(uint8_t level);

  // Direct setting write used by the select entities. The official Anker app
  // uses ATT Write Command (write without response) on characteristic 0x7777
  // for these setting commands on both the EverFrost 30 and EverFrost 50.
  void send_setting_command_no_response(uint8_t command, uint8_t value) {
#ifdef USE_ESP32
    if (value > 2 || this->model_ == MODEL_UNKNOWN)
      return;

    if (!this->ready_ || this->write_handle_ == 0 || this->parent_ == nullptr)
      return;

    std::vector<uint8_t> packet{
        0x08, 0xEE, 0x00, 0x00, 0x00, 0x02, command, 0x0B, 0x00, value,
    };
    packet.push_back(this->checksum_(packet.data(), packet.size()));
    this->log_packet_("TX", packet.data(), packet.size());

    auto status = esp_ble_gattc_write_char(
        this->parent_->get_gattc_if(), this->parent_->get_conn_id(), this->write_handle_,
        packet.size(), packet.data(), ESP_GATT_WRITE_TYPE_NO_RSP,
        ESP_GATT_AUTH_REQ_NONE);

    if (status == ESP_OK)
      this->schedule_status_refresh_();
#else
    (void) command;
    (void) value;
#endif
  }

 protected:
  void parse_packet_(const uint8_t *data, uint16_t length);
  bool validate_checksum_(const uint8_t *data, uint16_t length) const;
  uint8_t checksum_(const uint8_t *data, uint16_t length) const;
  void write_packet_(const std::vector<uint8_t> &packet);
  void send_startup_request_();
  void send_target_temperature_(uint8_t zone, float temperature_c);
  void send_zone_power_(uint8_t zone, bool enabled);
  void send_setting_(uint8_t command, uint8_t value);
  void publish_current_temperature_(float temperature_c);
  void publish_target_temperature_(float temperature_c);
  void publish_zone2_current_temperature_(float temperature_c);
  void publish_zone2_target_temperature_(float temperature_c);
  void publish_zone_power_(uint8_t zone, bool enabled);
  void publish_battery_(uint8_t battery);
  void publish_voltage_protection_(uint8_t level);
  void publish_screen_brightness_(uint8_t level);
  void log_packet_(const char *prefix, const uint8_t *data, uint16_t length) const;
  void schedule_status_refresh_();

  static constexpr uint32_t SERVICE_UUID_30 = 0x0156F5DA;
  static constexpr uint32_t SERVICE_UUID_50 = 0x0158F5DA;
  static constexpr uint16_t WRITE_CHAR_UUID = 0x7777;
  static constexpr uint16_t NOTIFY_CHAR_UUID = 0x8888;

  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};
  bool ready_{false};
  bool raw_packet_logging_{false};
  Model model_{MODEL_UNKNOWN};
  int pending_target_f_{-999};
  int pending_zone2_target_f_{-999};

  sensor::Sensor *current_temperature_sensor_{nullptr};
  sensor::Sensor *battery_sensor_{nullptr};
  binary_sensor::BinarySensor *connected_binary_sensor_{nullptr};
  EverFrostZoneClimate *zone2_climate_{nullptr};
  select::Select *voltage_protection_select_{nullptr};
  select::Select *screen_brightness_select_{nullptr};
};

class EverFrostVoltageProtectionSelectDirect : public select::Select {
 public:
  void set_parent(EverFrostClimate *parent) { this->parent_ = parent; }

 protected:
  void control(size_t index) override {
    if (this->parent_ == nullptr || index > 2)
      return;
    this->parent_->send_setting_command_no_response(0x85, static_cast<uint8_t>(index));
    this->publish_state(index);
  }

  EverFrostClimate *parent_{nullptr};
};

class EverFrostScreenBrightnessSelectDirect : public select::Select {
 public:
  void set_parent(EverFrostClimate *parent) { this->parent_ = parent; }

 protected:
  void control(size_t index) override {
    if (this->parent_ == nullptr || index > 2)
      return;
    this->parent_->send_setting_command_no_response(0x81, static_cast<uint8_t>(index));
    this->publish_state(index);
  }

  EverFrostClimate *parent_{nullptr};
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
