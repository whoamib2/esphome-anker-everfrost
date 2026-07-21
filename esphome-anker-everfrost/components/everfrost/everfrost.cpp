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

void EverFrostClimate::setup() {
  this->mode = climate::CLIMATE_MODE_COOL;
  this->action = climate::CLIMATE_ACTION_IDLE;

  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_initial_state(false);
}

void EverFrostClimate::dump_config() {
  LOG_CLIMATE("", "Anker EverFrost", this);
  ESP_LOGCONFIG(TAG, "  Service UUID: 0x%08" PRIX32, SERVICE_UUID);
  ESP_LOGCONFIG(TAG, "  Write characteristic: 0x%04X", WRITE_CHAR_UUID);
  ESP_LOGCONFIG(TAG, "  Notify characteristic: 0x%04X", NOTIFY_CHAR_UUID);
  ESP_LOGCONFIG(TAG, "  Raw packet logging: %s", YESNO(this->raw_packet_logging_));
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Current Temperature Sensor", this->current_temperature_sensor_);
  LOG_SENSOR("  ", "Battery Sensor", this->battery_sensor_);
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_binary_sensor_);
}

climate::ClimateTraits EverFrostClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supported_modes({climate::CLIMATE_MODE_COOL});
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(-20.0f);
  traits.set_visual_max_temperature(20.0f);
  traits.set_visual_temperature_step(1.0f);
  return traits;
}

void EverFrostClimate::control(const climate::ClimateCall &call) {
  auto target = call.get_target_temperature();
  if (!target.has_value())
    return;

  this->send_target_temperature_(*target);
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

  auto status = esp_ble_gattc_write_char(
      this->parent_->get_gattc_if(), this->parent_->get_conn_id(), this->write_handle_,
      packet.size(), const_cast<uint8_t *>(packet.data()), ESP_GATT_WRITE_TYPE_RSP,
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

void EverFrostClimate::send_target_temperature_(float temperature_c) {
  // The protocol is Fahrenheit-based: encoded byte = whole degrees F + 128.
  int target_f = static_cast<int>(std::lround((temperature_c * 9.0f / 5.0f) + 32.0f));
  target_f = std::max(-20, std::min(68, target_f));

  std::vector<uint8_t> packet{
      0x08, 0xEE, 0x00, 0x00, 0x00, 0x02, 0x88, 0x0B, 0x00,
      static_cast<uint8_t>(target_f + 128),
  };
  packet.push_back(this->checksum_(packet.data(), packet.size()));

  this->pending_target_f_ = target_f;
  ESP_LOGI(TAG, "Setting target temperature to %d°F (%.1f°C)", target_f,
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

void EverFrostClimate::publish_battery_(uint8_t battery) {
  if (battery <= 100 && this->battery_sensor_ != nullptr)
    this->battery_sensor_->publish_state(battery);
}

void EverFrostClimate::parse_packet_(const uint8_t *data, uint16_t length) {
  this->log_packet_("RX", data, length);

  if (length < 10) {
    ESP_LOGW(TAG, "Ignoring short packet (%u bytes)", length);
    return;
  }

  if (!this->validate_checksum_(data, length)) {
    ESP_LOGW(TAG, "Ignoring packet with invalid checksum");
    return;
  }

  // Incoming packets observed so far begin 09 FF 00 00 01 01.
  const uint8_t type = data[6];

  switch (type) {
    case 0x01: {
      // Full 54-byte status response.
      if (length < 54) {
        ESP_LOGW(TAG, "Full-status packet was only %u bytes", length);
        return;
      }

      // Provisional: byte 13 has matched the cooler's displayed battery percentage.
      const uint8_t battery = data[13];
      const int current_f = static_cast<int>(data[17]) - 128;
      const int target_f = static_cast<int>(data[18]) - 128;
      const float current_c = (current_f - 32.0f) * 5.0f / 9.0f;
      const float target_c = (target_f - 32.0f) * 5.0f / 9.0f;

      ESP_LOGI(TAG, "Status: current=%d°F, target=%d°F, battery=%u%%",
               current_f, target_f, battery);

      this->publish_battery_(battery);
      this->publish_current_temperature_(current_c);
      this->publish_target_temperature_(target_c);

      if (this->pending_target_f_ != -999 && target_f == this->pending_target_f_) {
        ESP_LOGI(TAG, "Target-temperature change confirmed at %d°F", target_f);
        this->pending_target_f_ = -999;
      }
      break;
    }

    case 0x05: {
      // Current-temperature notification.
      const int current_f = static_cast<int>(data[9]) - 128;
      const float current_c = (current_f - 32.0f) * 5.0f / 9.0f;
      ESP_LOGD(TAG, "Current temperature notification: %d°F", current_f);
      this->publish_current_temperature_(current_c);
      break;
    }

    case 0x0B: {
      // Target-temperature notification.
      const int target_f = static_cast<int>(data[9]) - 128;
      const float target_c = (target_f - 32.0f) * 5.0f / 9.0f;
      ESP_LOGD(TAG, "Target temperature notification: %d°F", target_f);
      this->publish_target_temperature_(target_c);
      break;
    }

    case 0x88:
      ESP_LOGD(TAG, "Command acknowledgement received");
      // Ask for authoritative state after a successful write.
      this->set_timeout("post_write_refresh", 500, [this]() { this->request_status(); });
      break;

    case 0x03:
      ESP_LOGD(TAG, "Unknown telemetry 0x03 value=%u", data[9]);
      break;

    case 0x14:
      ESP_LOGD(TAG, "Unknown telemetry 0x14 value=%u", data[9]);
      break;

    default:
      ESP_LOGD(TAG, "Unhandled packet type 0x%02X", type);
      break;
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
      this->current_temperature = NAN;
      this->target_temperature = NAN;
      this->publish_state();
      if (this->connected_binary_sensor_ != nullptr)
        this->connected_binary_sensor_->publish_state(false);
      ESP_LOGW(TAG, "EverFrost disconnected");
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      const auto service_uuid = espbt::ESPBTUUID::from_uint32(SERVICE_UUID);
      const auto write_uuid = espbt::ESPBTUUID::from_uint16(WRITE_CHAR_UUID);
      const auto notify_uuid = espbt::ESPBTUUID::from_uint16(NOTIFY_CHAR_UUID);

      auto *write_chr = this->parent_->get_characteristic(service_uuid, write_uuid);
      auto *notify_chr = this->parent_->get_characteristic(service_uuid, notify_uuid);

      if (write_chr == nullptr || notify_chr == nullptr) {
        ESP_LOGE(TAG, "Required EverFrost BLE characteristics were not found");
        return;
      }

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
