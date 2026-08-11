#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "tcl_climate.h"
#include <map>

namespace esphome {
namespace tcl_climate {

static constexpr uint8_t REQ_CMD[] = {0xBB, 0x00, 0x01, 0x04, 0x02, 0x01, 0x00, 0xBD};
static constexpr int MAX_LINE_LENGTH = 100;
static constexpr int UPDATE_INTERVAL_MS = 2000;

void TCLClimate::set_current_temperature(float current_temperature) {
  if (std::abs(this->current_temperature - current_temperature) < 0.1f) return;
  ESP_LOGD("TCL", "Current temperature updated to: %.1f°C", current_temperature);
  this->is_changed = true;
  this->current_temperature = current_temperature;
}

void TCLClimate::set_custom_fan_mode(StringRef fan_mode) {
  StringRef current(this->get_custom_fan_mode());
  if (!current.empty() && fan_mode == current.c_str()) return;
  ESP_LOGI("TCL", "Fan mode changed to: %s", fan_mode.c_str());
  this->is_changed = true;
  this->set_custom_fan_mode_(fan_mode.c_str());
}

void TCLClimate::set_mode(climate::ClimateMode mode) {
  if (this->mode == mode) return;
  const char* mode_str = "";
  switch (mode) {
    case climate::CLIMATE_MODE_OFF: mode_str = "OFF"; break;
    case climate::CLIMATE_MODE_COOL: mode_str = "COOL"; break;
    case climate::CLIMATE_MODE_HEAT: mode_str = "HEAT"; break;
    case climate::CLIMATE_MODE_FAN_ONLY: mode_str = "FAN ONLY"; break;
    case climate::CLIMATE_MODE_DRY: mode_str = "DRY"; break;
    case climate::CLIMATE_MODE_AUTO: mode_str = "AUTO"; break;
    default: mode_str = "UNKNOWN"; break;
  }
  ESP_LOGI("TCL", "Climate mode changed to: %s", mode_str);
  this->is_changed = true;
  this->mode = mode;
}

void TCLClimate::set_swing_mode(climate::ClimateSwingMode swing_mode) {
  if (this->swing_mode == swing_mode) return;
  this->is_changed = true;
  this->swing_mode = swing_mode;
}

void TCLClimate::set_hswing_pos(const std::string &hswing_pos) { this->hswing_pos = hswing_pos; }
void TCLClimate::set_vswing_pos(const std::string &vswing_pos) { this->vswing_pos = vswing_pos; }

void TCLClimate::set_target_temperature(float target_temperature) {
  if (std::abs(this->target_temperature - target_temperature) < 0.1f) return;
  ESP_LOGI("TCL", "Target temperature changed to: %.1f°C", target_temperature);
  this->is_changed = true;
  this->target_temperature = target_temperature;
}

void TCLClimate::build_set_cmd(get_cmd_resp_t *get_cmd_resp) {
  memcpy(m_set_cmd.raw, set_cmd_base, sizeof(m_set_cmd.raw));

  m_set_cmd.data.power = (get_cmd_resp->data.power == 0x03) ? 1 : 0;

  m_set_cmd.data.off_timer_en = 0;
  m_set_cmd.data.on_timer_en = 0;
  m_set_cmd.data.beep = 0;
  m_set_cmd.data.disp = 1;
  m_set_cmd.data.eco = 0;

  if (get_cmd_resp->data.fan == 0x0F) {
    m_set_cmd.data.turbo = 1;
    m_set_cmd.data.fan = 0x05;
  } else {
    m_set_cmd.data.turbo = 0;
    m_set_cmd.data.fan = get_cmd_resp->data.fan;
  }

  m_set_cmd.data.mode = get_cmd_resp->data.mode;
  m_set_cmd.data.fan = get_cmd_resp->data.fan;
  m_set_cmd.data.temp = 15 - get_cmd_resp->data.temp;

  m_set_cmd.data.vswing = 0;
  m_set_cmd.data.hswing = 0;
  m_set_cmd.data.half_degree = 0;

  // Розрахунок XOR
  uint8_t xor_byte = 0;
  for (size_t i = 0; i < sizeof(m_set_cmd.raw) - 1; i++) {
    xor_byte ^= m_set_cmd.raw[i];
  }
  m_set_cmd.raw[sizeof(m_set_cmd.raw) - 1] = xor_byte;
}

void TCLClimate::setup() {
  set_update_interval(UPDATE_INTERVAL_MS);
  this->set_supported_custom_fan_modes({"Automatic", "Turbo", "1", "2", "3"});
}

void TCLClimate::control_vertical_swing(const std::string &swing_mode) {}
void TCLClimate::control_horizontal_swing(const std::string &swing_mode) {}

void TCLClimate::control(const climate::ClimateCall &call) {
  get_cmd_resp_t get_cmd_resp = {0};
  memcpy(get_cmd_resp.raw, m_get_cmd_resp.raw, sizeof(get_cmd_resp.raw));
  bool should_build_cmd = false;

  // 🔥 ОБРОБКА ЗАСУВКИ РЕЖИМУ:
  // Якщо викликом передано новий режим — фіксуємо його в pending_mode.
  // Якщо викликом передано тільки температуру — беремо pending_mode, щоб не скидати режим на застарілий this->mode.
  climate::ClimateMode active_mode;
  if (call.get_mode().has_value()) {
    active_mode = *call.get_mode();
    this->pending_mode = active_mode;
    this->has_pending_mode = true;
  } else if (this->has_pending_mode) {
    active_mode = this->pending_mode;
  } else {
    active_mode = this->mode;
  }

  if (active_mode == climate::CLIMATE_MODE_OFF) {
    get_cmd_resp.data.power = 0x02;
    get_cmd_resp.data.mode = 0x00;
  } else {
    get_cmd_resp.data.power = 0x03;
    switch (active_mode) {
      case climate::CLIMATE_MODE_HEAT:     get_cmd_resp.data.mode = 0x01; break;
      case climate::CLIMATE_MODE_DRY:      get_cmd_resp.data.mode = 0x02; break;
      case climate::CLIMATE_MODE_COOL:     get_cmd_resp.data.mode = 0x03; break;
      case climate::CLIMATE_MODE_FAN_ONLY: get_cmd_resp.data.mode = 0x07; break;
      case climate::CLIMATE_MODE_AUTO:     get_cmd_resp.data.mode = 0x08; break;
      default:                             get_cmd_resp.data.mode = 0x03; break;
    }
  }
  should_build_cmd = true;

  if (call.get_target_temperature().has_value()) {
    float temp = *call.get_target_temperature();
    get_cmd_resp.data.temp = static_cast<uint8_t>(temp) - 16;
  } else {
    get_cmd_resp.data.temp = static_cast<uint8_t>(this->target_temperature) - 16;
  }

  std::string active_fan = this->get_custom_fan_mode();
  if (!call.get_custom_fan_mode().empty()) {
    active_fan = call.get_custom_fan_mode();
  }

  bool is_turbo_selected = false;
  if (active_fan == "1") {
    get_cmd_resp.data.fan = 0x02;
  } else if (active_fan == "2") {
    get_cmd_resp.data.fan = 0x03;
  } else if (active_fan == "3") {
    get_cmd_resp.data.fan = 0x05;
  } else if (active_fan == "Turbo") {
    get_cmd_resp.data.fan = 0x05;
    is_turbo_selected = true;
  } else {
    get_cmd_resp.data.fan = 0x00;
  }

  if (should_build_cmd) {
    build_set_cmd(&get_cmd_resp);

    m_set_cmd.raw[5] = (m_set_cmd.raw[5] & 0xF0) | (get_cmd_resp.data.mode & 0x0F);

    if (is_turbo_selected) {
      m_set_cmd.raw[7] |= 0x80;
    }

    uint8_t xor_byte = 0;
    for (size_t i = 0; i < sizeof(m_set_cmd.raw) - 1; i++) {
      xor_byte ^= m_set_cmd.raw[i];
    }
    m_set_cmd.raw[sizeof(m_set_cmd.raw) - 1] = xor_byte;

    ready_to_send_set_cmd_flag = true;
  }
}

climate::ClimateTraits TCLClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_supported_modes({
    climate::CLIMATE_MODE_OFF,
    climate::CLIMATE_MODE_COOL,
    climate::CLIMATE_MODE_HEAT,
    climate::CLIMATE_MODE_DRY,
    climate::CLIMATE_MODE_FAN_ONLY,
    climate::CLIMATE_MODE_AUTO
  });
  traits.set_visual_min_temperature(16.0);
  traits.set_visual_max_temperature(31.0);
  traits.set_visual_target_temperature_step(1.0);
  return traits;
}

void TCLClimate::update() {
  if (ready_to_send_set_cmd_flag) {
    ready_to_send_set_cmd_flag = false;
    write_array(m_set_cmd.raw, sizeof(m_set_cmd.raw));
  } else {
    write_array(REQ_CMD, sizeof(REQ_CMD));
  }
}

int TCLClimate::read_data_line(int readch, uint8_t *buffer, int len) {
  static int pos = 0;
  if (readch < 0) return -1;

  if (readch == 0xBB && pos == 0) {
    buffer[pos++] = readch;
    return -1;
  }
  if (pos > 0 && pos < len) {
    buffer[pos++] = readch;
    if (pos == 21) {
      int final_len = pos;
      pos = 0;
      return final_len;
    }
  }
  return -1;
}

bool TCLClimate::is_valid_xor(uint8_t *buffer, int len) {
  if (len < 1) return false;
  uint8_t xor_byte = 0;
  for (int i = 0; i < len - 1; i++) { xor_byte ^= buffer[i]; }
  return xor_byte == buffer[len - 1];
}

void TCLClimate::print_hex_str(uint8_t *buffer, int len) {
  char str[MAX_LINE_LENGTH * 3] = {0};
  char *pstr = str;
  for (int i = 0; i < len && (pstr - str) < sizeof(str) - 3; i++) {
    pstr += sprintf(pstr, "%02X ", buffer[i]);
  }
  ESP_LOGD("TCL", "Парсинг пакету: %s", str);
}

void TCLClimate::loop() {
  static uint8_t buffer[MAX_LINE_LENGTH];

  while (available()) {
    int len = read_data_line(read(), buffer, MAX_LINE_LENGTH);
    if (len == 21 && buffer[0] == 0xBB && buffer[1] == 0x01) {

      if (is_valid_xor(buffer, len)) {
        memcpy(m_get_cmd_resp.raw, buffer, len);
        print_hex_str(buffer, len);

        this->is_changed = true;

        uint8_t byte7 = m_get_cmd_resp.raw[7];
        uint8_t low_nibble  = (byte7 & 0x0F);
        uint8_t high_nibble = (byte7 & 0xF0);

        if (low_nibble == 0x00 || high_nibble == 0x20) {
          this->set_mode(climate::CLIMATE_MODE_OFF);
        } else {
          switch (low_nibble) {
            case 0x01: this->set_mode(climate::CLIMATE_MODE_COOL); break;
            case 0x02: this->set_mode(climate::CLIMATE_MODE_FAN_ONLY); break;
            case 0x03: this->set_mode(climate::CLIMATE_MODE_DRY); break;
            case 0x04: this->set_mode(climate::CLIMATE_MODE_HEAT); break;
            case 0x05: this->set_mode(climate::CLIMATE_MODE_AUTO); break;
            default:   this->set_mode(climate::CLIMATE_MODE_COOL); break;
          }
        }

        uint8_t byte8 = m_get_cmd_resp.raw[8];
        uint8_t fan_raw = (byte8 & 0xF0) >> 4;
        uint8_t temp_raw = (byte8 & 0x0F);

        switch (fan_raw) {
          case 0x01: this->set_custom_fan_mode(StringRef("1")); break;
          case 0x02: this->set_custom_fan_mode(StringRef("2")); break;
          case 0x03: this->set_custom_fan_mode(StringRef("3")); break;
          case 0x00: this->set_custom_fan_mode(StringRef("Automatic")); break;
          default:   this->set_custom_fan_mode(StringRef("Automatic")); break;
        }

        float target_t = temp_raw + 16;
        this->set_target_temperature(target_t);

        uint8_t raw_current_temp = m_get_cmd_resp.raw[17];
        if (raw_current_temp > 0) {
          float calculated_current_temp = (static_cast<float>(raw_current_temp) / 3.0f) - 11.33f;
          this->set_current_temperature(calculated_current_temp);
        } else {
          this->set_current_temperature(target_t);
        }

        // 🔥 СКИДАННЯ ЗАСУВКИ ПІСЛЯ ПІДТВЕРДЖЕННЯ ВІД КЛИМАТУ
        // Якщо кондиціонер прислав оновлення і в черзі немає нової команди від HA — розблоковуємо синхронізацію для ІЧ-пульта
        if (!this->ready_to_send_set_cmd_flag) {
          this->has_pending_mode = false;
          this->pending_mode = this->mode;
        }

        if (this->is_changed) {
          this->publish_state();
        }
      }
    }
  }
}

}  // namespace tcl_climate
}  // namespace esphome
