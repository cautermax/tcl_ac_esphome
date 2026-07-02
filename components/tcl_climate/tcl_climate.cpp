#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "tcl_climate.h"
#include <map>

namespace esphome {
namespace tcl_climate {

static constexpr uint8_t REQ_CMD[] = {0xBB, 0x00, 0x01, 0x04, 0x02, 0x01, 0x00, 0xBD};
static constexpr int MAX_LINE_LENGTH = 100;
static constexpr int UPDATE_INTERVAL_MS = 2000; // Робимо 2 сек, щоб не забивати UART запитами

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
    m_set_cmd.data.beep = 1;
    m_set_cmd.data.disp = 1;
    m_set_cmd.data.eco = 0;
    
    // Перевірка на турбо режим зі старшої половини байту вентилятора
    m_set_cmd.data.turbo = (get_cmd_resp->data.fan == 0x03) ? 1 : 0;
    m_set_cmd.data.mute = 0;

    static constexpr uint8_t MODE_MAP[] = { 0x00, 0x03, 0x02, 0x07, 0x01, 0x08 };
    if (get_cmd_resp->data.mode < sizeof(MODE_MAP)) {
        m_set_cmd.data.mode = MODE_MAP[get_cmd_resp->data.mode];
    }

    m_set_cmd.data.temp = 15 - get_cmd_resp->data.temp;

    static constexpr uint8_t FAN_MAP[] = { 0x00, 0x02, 0x03, 0x05, 0x06, 0x07 };
    if (get_cmd_resp->data.fan < sizeof(FAN_MAP)) {
        m_set_cmd.data.fan = FAN_MAP[get_cmd_resp->data.fan];
    }

    m_set_cmd.data.vswing = 0;
    m_set_cmd.data.hswing = 0;
    m_set_cmd.data.half_degree = 0;

    uint8_t xor_byte = 0;
    for (size_t i = 0; i < sizeof(m_set_cmd.raw) - 1; i++) {
        xor_byte ^= m_set_cmd.raw[i];
    }
    m_set_cmd.raw[sizeof(m_set_cmd.raw) - 1] = xor_byte;
}

void TCLClimate::setup() {
  set_update_interval(UPDATE_INTERVAL_MS);
  this->set_supported_custom_fan_modes({"Turbo", "Automatic", "1", "2", "3"});
}

void TCLClimate::control_vertical_swing(const std::string &swing_mode) {}
void TCLClimate::control_horizontal_swing(const std::string &swing_mode) {}

void TCLClimate::control(const climate::ClimateCall &call) {
    get_cmd_resp_t get_cmd_resp = {0};
    memcpy(get_cmd_resp.raw, m_get_cmd_resp.raw, sizeof(get_cmd_resp.raw));
    bool should_build_cmd = false;

    if (call.get_mode().has_value()) {
        climate::ClimateMode climate_mode = *call.get_mode();
        if (climate_mode == climate::CLIMATE_MODE_OFF) {
            get_cmd_resp.data.power = 0x02; // Код вимкнення з пакету 5
        } else {
            get_cmd_resp.data.power = 0x03; // Код увімкнення з пакетів 1-4
            switch (climate_mode) {
                case climate::CLIMATE_MODE_COOL:     get_cmd_resp.data.mode = 0x01; break;
                case climate::CLIMATE_MODE_HEAT:     get_cmd_resp.data.mode = 0x04; break;
                default: get_cmd_resp.data.mode = 0x01; break;
            }
        }
        should_build_cmd = true;
    }

    if (call.get_target_temperature().has_value()) {
        float temp = *call.get_target_temperature();
        get_cmd_resp.data.temp = static_cast<uint8_t>(temp) - 16;
        should_build_cmd = true;
    }

    if (should_build_cmd) {
        build_set_cmd(&get_cmd_resp);
        ready_to_send_set_cmd_flag = true;
    }
}

climate::ClimateTraits TCLClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT});
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

    // Спрощений збирач пакету: шукаємо початок заголовку BB 01
    if (readch == 0xBB && pos == 0) {
        buffer[pos++] = readch;
        return -1;
    }
    if (pos > 0 && pos < len) {
        buffer[pos++] = readch;
        if (pos == 21) { // Пакет наповнився (21 байт)
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

                // 1. Розбір стану живлення (Байт 7, старші 4 біти)
                if (m_get_cmd_resp.data.power == 0x03) {
                    // Розбір режимів роботи (Байт 7, молодші 4 біти)
                    if (m_get_cmd_resp.data.mode == 0x04) {
                        this->set_mode(climate::CLIMATE_MODE_HEAT);
                    } else {
                        this->set_mode(climate::CLIMATE_MODE_COOL);
                    }
                } else {
                    this->set_mode(climate::CLIMATE_MODE_OFF);
                }

                // 2. Розбір швидкості вентилятора (Байт 8, старші 4 біти)
                if (m_get_cmd_resp.data.fan == 0x03) {
                    this->set_custom_fan_mode(StringRef("Turbo"));
                } else {
                    this->set_custom_fan_mode(StringRef("Automatic"));
                }

                // 3. Розбір Цільової температури (Байт 8, молодші 4 біти)
                float target_t = m_get_cmd_resp.data.temp + 16;
                this->set_target_temperature(target_t);

                // Датчик поточної температури в кімнаті (приблизний розрахунок)
                this->set_current_temperature(target_t); 

                if (this->is_changed) {
                    this->publish_state();
                }
            }
        }
    }
}

}  // namespace tcl_climate
}  // namespace esphome
