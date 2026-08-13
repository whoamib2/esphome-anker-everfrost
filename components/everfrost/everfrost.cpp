#include "everfrost.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <string>

#include "esphome/core/log.h"

namespace esphome {
namespace everfrost {

static const char *const TAG = "everfrost";

static climate::ClimateTraits everfrost_traits(bool supports_off) {
  auto traits = climate::ClimateTraits();
  if (supports_off)
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_COOL});
  else
    traits.set_supported_modes({climate::CLIMATE_MODE_COOL});
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(-20.0f);
  traits.set_visual_max_temperature(20.0f);
  traits.set_visual_temperature_step(1.0f);
  return traits;
}

climate::ClimateTraits EverFrostZoneClimate::traits() { return everfrost_traits(true); }

void EverFrostZoneClimate::control(const climate::ClimateCall &call) {
  if (this->parent_ == nullptr)
    return;

  auto mode = call.get_mode();
  if (mode.has_value()) {
    if (*mode == climate::CLIMATE_MODE_OFF) {
      this->parent_->set_zone_power(2, false);
      return;
    }
    if (*mode == climate::CLIMATE_MODE_COOL)
      this->parent_->set_zone_power(2, true);
  }

  auto target = call.get_target_temperature();
  if (target.has_value())
    this->parent_->set_zone_target_temperature(2, *target);
}

void EverFrostZoneClimate::publish_current_temperature_value(float temperature_c) {
  this->current_temperature = temperature_c;
  this->publish_state();
}

void EverFrostZoneClimate::publish_target_temperature_value(float temperature_c) {
  this->target_temperature = temperature_c;
  this->publish_state();
}

void EverFrostZoneClimate::publish_power_value(bool enabled) {
  this->mode = enabled ? climate::CLIMATE_MODE_COOL : climate::CLIMATE_MODE_OFF;
  this->action = enabled ? climate::CLIMATE_ACTION_IDLE : climate::CLIMATE_ACTION_OFF;
  this->publish_state();
}

void EverFrostZoneClimate::publish_disconnected() {
  this->current_temperature = NAN;
  this->target_temperature = NAN;
  this->publish_state();
}

void EverFrostVoltageProtectionSelect::control(size_t index) {
  if (this->parent_ == nullptr || index > 2)
    return;
  this->parent_->set_voltage_protection(static_cast<uint8_t>(index));
  this->publish_state(index);
}

void EverFrostScreenBrightnessSelect::control(size_t index) {
  if (this->parent_ == nullptr || index > 2)
    return;
  this->parent_->set_screen_brightness(static_cast<uint8_t>(index));
  this->publish_state(index);
}

void EverFrostClimate::setup() {
  this->mode = climate::CLIMATE_MODE_COOL;
  this->action = climate::CLIMATE_ACTION_IDLE;

  if (this->zone2_climate_ != nullptr) {
    this->zone2_climate_->mode = climate::CLIMATE_MODE_COOL;
    this->zone2_climate_->action = climate::CLIMATE_ACTION_IDLE;
  }

  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_initial_state(false);
}

void EverFrostClimate::dump_config() {
  LOG_CLIMATE("", "Anker EverFrost", this);
  ESP_LOGCONFIG(TAG, "  EverFrost 30 service UUID: 0x%08" PRIX32, SERVICE_UUID_30);
  ESP_LOGCONFIG(TAG, "  EverFrost 50 service UUID: 0x%08" PRIX32, SERVICE_UUID_50);
  ESP_LOGCONFIG(TAG, "  Write characteristic: 0x%04X", WRITE_CHAR_UUID);
  ESP_LOGCONFIG(TAG, "  Notify characteristic: 0x%04X", NOTIFY_CHAR_UUID);
  ESP_LOGCONFIG(TAG, "  Zone 2 climate configured: %s", YESNO(this->zone2_climate_ != nullptr));
  ESP_LOGCONFIG(TAG, "  Raw packet logging: %s", YESNO(this->raw_packet_logging_));
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Current Temperature Sensor", this->current_temperature_sensor_);
  LOG_SENSOR("  ", "Battery Sensor", this->battery_sensor_);
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_binary_sensor_);
}

climate::ClimateTraits EverFrostClimate::traits() {
  // The 30L has no independent cooling-off command; its climate remains COOL-only.
  // A configured Zone 2 identifies the 50L setup and enables OFF/COOL for Zone 1.
  return everfrost_traits(this->zone2_climate_ != nullptr);
}

void EverFrostClimate::control(const climate::ClimateCall &call) {
  auto mode = call.get_mode();
  if (mode.has_value() && this->zone2_climate_ != nullptr) {
    if (*mode == climate::CLIMATE_MODE_OFF) {
      this->set_zone_power(1, false);
      return;
    }
    if (*mode == climate::CLIMATE_MODE_COOL)
      this->set_zone_power(1, true);
  }

  auto target = call.get_target_temperature();
  if (target.has_value())
    this->send_target_temperature_(1, *target);
}

void EverFrostClimate::set_zone_target_temperature(uint8_t zone, float temperature_c) {
  this->send_target_temperature_(zone, temperature_c);
}

void EverFrostClimate::set_zone_power(uint8_t zone, bool enabled) {
  this->send_zone_power_(zone, enabled);
}

void EverFrostClimate::set_voltage_protection(uint8_t level) {
  if (level > 2)
    return;
  this->send_setting_(0x85, level);
}

void EverFrostClimate::set_screen_brightness(uint8_t level) {
  if (level > 2)
    return;
  this->send_setting_(0x81, level);
}

void EverFrostClimate::update() { this->request_status(); }

void EverFrostClimate::request_status() {
  if (!this->ready_) {
    ESP_LOGD(TAG, "Status request skipped because BLE is not ready");
    return;
  }
  this->send_startup_request_();
}

uint8_t EverFrostClimate::checksum_(const uint8_t *data, uint16_t length) const {
  uint16_t sum = 0;
  for (uint16_t i = 0; i < length; i++)
    sum += data[i];
  return static_cast<uint8_t>(sum & 0xFF);
}

bool EverFrostClimate::validate_checksum_(const uint8_t *data, uint16_t length) const {
  if (length < 2)
    return false;
  return this->checksum_(data, length - 1) == data[length - 1];
}

void EverFrostClimate::write_packet_(const std::vector<uint8_t> &packet) {
#ifdef USE_ESP32
  if (!this->ready_ || this->write_handle_ == 0 || this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Cannot write: EverFrost BLE connection is not ready");
    return;
  }

  this->log_packet_("TX", packet.data(), packet.size());

  const auto write_type = this->model_ == MODEL_50 ? ESP_GATT_WRITE_TYPE_NO_RSP
                                                    : ESP_GATT_WRITE_TYPE_RSP;
  auto status = esp_ble_gattc_write_char(
      this->parent_->get_gattc_if(), this->parent_->get_conn_id(), this->write_handle_,
      packet.size(), const_cast<uint8_t *>(packet.data()), write_type,
      ESP_GATT_AUTH_REQ_NONE);

  if (status != ESP_OK)
    ESP_LOGW(TAG, "BLE write failed, status=%d", status);
#else
  (void) packet;
#endif
}

void EverFrostClimate::send_startup_request_() {
  std::vector<uint8_t> packet{0x08, 0xEE, 0x00, 0x00, 0x00, 0x01, 0x01, 0x0A, 0x00};
  packet.push_back(this->checksum_(packet.data(), packet.size()));
  this->write_packet_(packet);
}

void EverFrostClimate::send_zone_power_(uint8_t zone, bool enabled) {
  if (this->model_ != MODEL_50) {
    ESP_LOGW(TAG, "Zone power control is only supported on the EverFrost 50 protocol");
    return;
  }

  uint8_t command = 0;
  if (zone == 1)
    command = 0x86;
  else if (zone == 2)
    command = 0x87;
  else {
    ESP_LOGW(TAG, "Invalid EverFrost 50 zone %u", zone);
    return;
  }

  std::vector<uint8_t> packet{
      0x08, 0xEE, 0x00, 0x00, 0x00, 0x02, command, 0x0B, 0x00,
      static_cast<uint8_t>(enabled ? 1 : 0),
  };
  packet.push_back(this->checksum_(packet.data(), packet.size()));

  ESP_LOGI(TAG, "Setting Zone %u power %s", zone, enabled ? "ON" : "OFF");
  this->write_packet_(packet);

  // Publish immediately for responsive HA controls. The command ACK schedules a
  // status refresh, which then confirms the real physical zone state.
  this->publish_zone_power_(zone, enabled);
}

void EverFrostClimate::send_setting_(uint8_t command, uint8_t value) {
  if (this->model_ != MODEL_50) {
    ESP_LOGW(TAG, "EverFrost setting command 0x%02X is only supported on the 50L protocol", command);
    return;
  }

  std::vector<uint8_t> packet{
      0x08, 0xEE, 0x00, 0x00, 0x00, 0x02, command, 0x0B, 0x00, value,
  };
  packet.push_back(this->checksum_(packet.data(), packet.size()));
  this->write_packet_(packet);
}

void EverFrostClimate::send_target_temperature_(uint8_t zone, float temperature_c) {
  int target_f = static_cast<int>(std::lround((temperature_c * 9.0f / 5.0f) + 32.0f));
  target_f = std::max(-20, std::min(68, target_f));

  uint8_t command = 0x88;
  if (this->model_ == MODEL_50) {
    if (zone == 1)
      command = 0x83;
    else if (zone == 2)
      command = 0x84;
    else {
      ESP_LOGW(TAG, "Invalid EverFrost 50 zone %u", zone);
      return;
    }

    // A temperature-only update does not implicitly change zone power.
  } else if (zone != 1) {
    ESP_LOGW(TAG, "Zone 2 is only available on a dual-zone EverFrost model");
    return;
  }

  std::vector<uint8_t> packet{
      0x08, 0xEE, 0x00, 0x00, 0x00, 0x02, command, 0x0B, 0x00,
      static_cast<uint8_t>(target_f + 128),
  };
  packet.push_back(this->checksum_(packet.data(), packet.size()));

  if (zone == 1)
    this->pending_target_f_ = target_f;
  else
    this->pending_zone2_target_f_ = target_f;

  ESP_LOGI(TAG, "Setting Zone %u target temperature to %d°F (%.1f°C)", zone, target_f,
           (target_f - 32.0f) * 5.0f / 9.0f);
  this->write_packet_(packet);
}

void EverFrostClimate::publish_current_temperature_(float temperature_c) {
  this->current_temperature = temperature_c;
  if (this->current_temperature_sensor_ != nullptr)
    this->current_temperature_sensor_->publish_state(temperature_c);
  this->publish_state();
}

void EverFrostClimate::publish_target_temperature_(float temperature_c) {
  this->target_temperature = temperature_c;
  this->publish_state();
}

void EverFrostClimate::publish_zone2_current_temperature_(float temperature_c) {
  if (this->zone2_climate_ != nullptr)
    this->zone2_climate_->publish_current_temperature_value(temperature_c);
}

void EverFrostClimate::publish_zone2_target_temperature_(float temperature_c) {
  if (this->zone2_climate_ != nullptr)
    this->zone2_climate_->publish_target_temperature_value(temperature_c);
}

void EverFrostClimate::publish_zone_power_(uint8_t zone, bool enabled) {
  if (zone == 1) {
    this->mode = enabled ? climate::CLIMATE_MODE_COOL : climate::CLIMATE_MODE_OFF;
    this->action = enabled ? climate::CLIMATE_ACTION_IDLE : climate::CLIMATE_ACTION_OFF;
    this->publish_state();
  } else if (zone == 2 && this->zone2_climate_ != nullptr) {
    this->zone2_climate_->publish_power_value(enabled);
  }
}

void EverFrostClimate::publish_battery_(uint8_t battery) {
  if (battery <= 100 && this->battery_sensor_ != nullptr)
    this->battery_sensor_->publish_state(battery);
}

void EverFrostClimate::publish_voltage_protection_(uint8_t level) {
  if (level <= 2 && this->voltage_protection_select_ != nullptr)
    this->voltage_protection_select_->publish_state(level);
}

void EverFrostClimate::publish_screen_brightness_(uint8_t level) {
  if (level <= 2 && this->screen_brightness_select_ != nullptr)
    this->screen_brightness_select_->publish_state(level);
}

void EverFrostClimate::schedule_status_refresh_() {
  this->set_timeout("post_write_refresh", 500, [this]() { this->request_status(); });
}

void EverFrostClimate::parse_packet_(const uint8_t *data, uint16_t length) {
  this->log_packet_("RX", data, length);

  auto parse_frame = [this](const uint8_t *frame, uint16_t frame_length) {
    if (frame_length < 10) {
      ESP_LOGW(TAG, "Ignoring short EverFrost frame (%u bytes)", frame_length);
      return;
    }

    if (!this->validate_checksum_(frame, frame_length)) {
      ESP_LOGW(TAG, "Ignoring EverFrost frame with invalid checksum");
      return;
    }

    const uint8_t type = frame[6];

    // Command acknowledgements use 09 FF 00 00 01 02 CMD 0A 00 CS.
    if (frame[5] == 0x02) {
      ESP_LOGD(TAG, "Command 0x%02X acknowledgement received", type);
      this->schedule_status_refresh_();
      return;
    }

    switch (type) {
      case 0x01: {
        if (frame_length < 54) {
          ESP_LOGW(TAG, "Full-status packet was only %u bytes", frame_length);
          return;
        }

        const uint8_t battery = frame[13];
        this->publish_battery_(battery);

        if (this->model_ == MODEL_50) {
          // Confirmed EverFrost 50 full-status layout.
          const uint8_t screen_brightness = frame[16];
          const uint8_t voltage_protection = frame[19];
          const int zone1_current_f = static_cast<int>(frame[20]) - 128;
          const int zone1_target_f = static_cast<int>(frame[21]) - 128;
          const bool zone1_enabled = frame[22] != 0;
          const int zone2_current_f = static_cast<int>(frame[23]) - 128;
          const int zone2_target_f = static_cast<int>(frame[24]) - 128;
          const bool zone2_enabled = frame[25] != 0;

          const float zone1_current_c = (zone1_current_f - 32.0f) * 5.0f / 9.0f;
          const float zone1_target_c = (zone1_target_f - 32.0f) * 5.0f / 9.0f;
          const float zone2_current_c = (zone2_current_f - 32.0f) * 5.0f / 9.0f;
          const float zone2_target_c = (zone2_target_f - 32.0f) * 5.0f / 9.0f;

          ESP_LOGI(TAG,
                   "Status 50: Z1 %s current=%d°F target=%d°F, Z2 %s current=%d°F target=%d°F, "
                   "battery=%u%%, brightness=%u, voltage=%u",
                   zone1_enabled ? "on" : "off", zone1_current_f, zone1_target_f,
                   zone2_enabled ? "on" : "off", zone2_current_f, zone2_target_f,
                   battery, screen_brightness, voltage_protection);

          this->publish_zone_power_(1, zone1_enabled);
          this->publish_zone_power_(2, zone2_enabled);
          this->publish_screen_brightness_(screen_brightness);
          this->publish_voltage_protection_(voltage_protection);
          this->publish_current_temperature_(zone1_current_c);
          this->publish_target_temperature_(zone1_target_c);
          this->publish_zone2_current_temperature_(zone2_current_c);
          this->publish_zone2_target_temperature_(zone2_target_c);

          if (this->pending_target_f_ != -999 && zone1_target_f == this->pending_target_f_) {
            ESP_LOGI(TAG, "Zone 1 target-temperature change confirmed at %d°F", zone1_target_f);
            this->pending_target_f_ = -999;
          }
          if (this->pending_zone2_target_f_ != -999 &&
              zone2_target_f == this->pending_zone2_target_f_) {
            ESP_LOGI(TAG, "Zone 2 target-temperature change confirmed at %d°F", zone2_target_f);
            this->pending_zone2_target_f_ = -999;
          }
        } else {
          const int current_f = static_cast<int>(frame[17]) - 128;
          const int target_f = static_cast<int>(frame[18]) - 128;
          const float current_c = (current_f - 32.0f) * 5.0f / 9.0f;
          const float target_c = (target_f - 32.0f) * 5.0f / 9.0f;

          ESP_LOGI(TAG, "Status 30: current=%d°F, target=%d°F, battery=%u%%",
                   current_f, target_f, battery);

          this->publish_current_temperature_(current_c);
          this->publish_target_temperature_(target_c);

          if (this->pending_target_f_ != -999 && target_f == this->pending_target_f_) {
            ESP_LOGI(TAG, "Target-temperature change confirmed at %d°F", target_f);
            this->pending_target_f_ = -999;
          }
        }
        break;
      }

      case 0x04: {
        if (this->model_ == MODEL_50) {
          const uint8_t battery = frame[9];
          ESP_LOGD(TAG, "Battery notification: %u%%", battery);
          this->publish_battery_(battery);
        }
        break;
      }

      case 0x05: {
        if (this->model_ == MODEL_30) {
          const int current_f = static_cast<int>(frame[9]) - 128;
          const float current_c = (current_f - 32.0f) * 5.0f / 9.0f;
          ESP_LOGD(TAG, "Current temperature notification: %d°F", current_f);
          this->publish_current_temperature_(current_c);
        }
        break;
      }

      case 0x06: {
        if (this->model_ == MODEL_50) {
          const int current_f = static_cast<int>(frame[9]) - 128;
          const float current_c = (current_f - 32.0f) * 5.0f / 9.0f;
          ESP_LOGD(TAG, "Zone 1 current temperature notification: %d°F", current_f);
          this->publish_current_temperature_(current_c);
        }
        break;
      }

      case 0x07: {
        if (this->model_ == MODEL_50) {
          const int current_f = static_cast<int>(frame[9]) - 128;
          const float current_c = (current_f - 32.0f) * 5.0f / 9.0f;
          ESP_LOGD(TAG, "Zone 2 current temperature notification: %d°F", current_f);
          this->publish_zone2_current_temperature_(current_c);
        }
        break;
      }

      case 0x0B: {
        if (this->model_ == MODEL_30) {
          const int target_f = static_cast<int>(frame[9]) - 128;
          const float target_c = (target_f - 32.0f) * 5.0f / 9.0f;
          ESP_LOGD(TAG, "Target temperature notification: %d°F", target_f);
          this->publish_target_temperature_(target_c);
        }
        break;
      }

      case 0x0C: {
        if (this->model_ == MODEL_50) {
          const int target_f = static_cast<int>(frame[9]) - 128;
          const float target_c = (target_f - 32.0f) * 5.0f / 9.0f;
          ESP_LOGD(TAG, "Zone 1 target temperature notification: %d°F", target_f);
          this->publish_target_temperature_(target_c);
          if (this->pending_target_f_ == target_f)
            this->pending_target_f_ = -999;
        }
        break;
      }

      case 0x0D: {
        if (this->model_ == MODEL_50) {
          const int target_f = static_cast<int>(frame[9]) - 128;
          const float target_c = (target_f - 32.0f) * 5.0f / 9.0f;
          ESP_LOGD(TAG, "Zone 2 target temperature notification: %d°F", target_f);
          this->publish_zone2_target_temperature_(target_c);
          if (this->pending_zone2_target_f_ == target_f)
            this->pending_zone2_target_f_ = -999;
        }
        break;
      }

      case 0x02:
        ESP_LOGD(TAG, "Unknown telemetry 0x02 value=%u", frame[9]);
        break;

      case 0x03:
        ESP_LOGD(TAG, "Unknown telemetry 0x03 value=%u", frame[9]);
        break;

      case 0x14:
        ESP_LOGD(TAG, "Unknown telemetry 0x14 value=%u", frame[9]);
        break;

      default:
        ESP_LOGD(TAG, "Unhandled packet type 0x%02X", type);
        break;
    }
  };

  // The cooler can concatenate multiple protocol frames into a single BLE
  // notification. Byte 7 contains the length of each individual frame.
  uint16_t offset = 0;
  while (offset < length) {
    const uint16_t remaining = length - offset;
    if (remaining < 8) {
      ESP_LOGW(TAG, "Ignoring %u trailing BLE byte(s) after EverFrost frame", remaining);
      return;
    }

    const uint8_t *frame = data + offset;
    uint16_t frame_length = frame[7];

    if (frame_length < 10 || frame_length > remaining) {
      // Fallback for an unexpected frame where the length byte cannot be used,
      // but the complete BLE notification itself has a valid checksum.
      if (offset == 0 && this->validate_checksum_(data, length)) {
        frame_length = length;
      } else {
        ESP_LOGW(TAG,
                 "Invalid EverFrost framing at offset %u: declared=%u remaining=%u",
                 offset, frame_length, remaining);
        return;
      }
    }

    if (offset != 0 || frame_length != length)
      this->log_packet_("RX frame", frame, frame_length);

    parse_frame(frame, frame_length);
    offset += frame_length;
  }
}

void EverFrostClimate::log_packet_(const char *prefix, const uint8_t *data,
                                   uint16_t length) const {
  if (!this->raw_packet_logging_)
    return;

  std::string out;
  out.reserve(length * 3);
  char byte_text[4];
  for (uint16_t i = 0; i < length; i++) {
    snprintf(byte_text, sizeof(byte_text), "%02X", data[i]);
    if (!out.empty())
      out += ' ';
    out += byte_text;
  }
  ESP_LOGD(TAG, "%s [%u]: %s", prefix, length, out.c_str());
}

#ifdef USE_ESP32
void EverFrostClimate::gattc_event_handler(esp_gattc_cb_event_t event,
                                           esp_gatt_if_t gattc_if,
                                           esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT:
      this->ready_ = false;
      this->write_handle_ = 0;
      this->notify_handle_ = 0;
      this->model_ = MODEL_UNKNOWN;
      this->current_temperature = NAN;
      this->target_temperature = NAN;
      this->publish_state();
      if (this->zone2_climate_ != nullptr)
        this->zone2_climate_->publish_disconnected();
      if (this->connected_binary_sensor_ != nullptr)
        this->connected_binary_sensor_->publish_state(false);
      ESP_LOGW(TAG, "EverFrost disconnected");
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      const auto write_uuid = espbt::ESPBTUUID::from_uint16(WRITE_CHAR_UUID);
      const auto notify_uuid = espbt::ESPBTUUID::from_uint16(NOTIFY_CHAR_UUID);

      auto service_uuid = espbt::ESPBTUUID::from_uint32(SERVICE_UUID_30);
      auto *write_chr = this->parent_->get_characteristic(service_uuid, write_uuid);
      auto *notify_chr = this->parent_->get_characteristic(service_uuid, notify_uuid);

      if (write_chr != nullptr && notify_chr != nullptr) {
        this->model_ = MODEL_30;
      } else {
        service_uuid = espbt::ESPBTUUID::from_uint32(SERVICE_UUID_50);
        write_chr = this->parent_->get_characteristic(service_uuid, write_uuid);
        notify_chr = this->parent_->get_characteristic(service_uuid, notify_uuid);
        if (write_chr != nullptr && notify_chr != nullptr)
          this->model_ = MODEL_50;
      }

      if (write_chr == nullptr || notify_chr == nullptr || this->model_ == MODEL_UNKNOWN) {
        ESP_LOGE(TAG, "Required EverFrost BLE characteristics were not found");
        return;
      }

      if (this->model_ == MODEL_50 && this->zone2_climate_ == nullptr)
        ESP_LOGW(TAG, "EverFrost 50 detected but no zone_2 climate is configured");

      ESP_LOGI(TAG, "Detected EverFrost %s BLE protocol",
               this->model_ == MODEL_50 ? "50" : "30");

      this->write_handle_ = write_chr->handle;
      this->notify_handle_ = notify_chr->handle;

      auto status = esp_ble_gattc_register_for_notify(
          this->parent_->get_gattc_if(), this->parent_->get_remote_bda(),
          this->notify_handle_);
      if (status != ESP_OK)
        ESP_LOGE(TAG, "Register-for-notify failed, status=%d", status);
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle != this->notify_handle_)
        break;
      this->ready_ = true;
      this->node_state = espbt::ClientState::ESTABLISHED;
      if (this->connected_binary_sensor_ != nullptr)
        this->connected_binary_sensor_->publish_state(true);
      ESP_LOGI(TAG, "EverFrost BLE connection is ready");
      this->set_timeout("initial_status", 250, [this]() { this->request_status(); });
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == this->notify_handle_)
        this->parse_packet_(param->notify.value, param->notify.value_len);
      break;

    default:
      break;
  }
}
#endif

}  // namespace everfrost
}  // namespace esphome
