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

    // Виставляємо біт живлення: 1 - увімкнено, 0 - вимкнено
    m_set_cmd.data.power = (get_cmd_resp->data.power == 0x03) ? 1 : 0;
    
    m_set_cmd.data.off_timer_en = 0;
    m_set_cmd.data.on_timer_en = 0;
    m_set_cmd.data.beep = 1;
    m_set_cmd.data.disp = 1;
    m_set_cmd.data.eco = 0;
    
    // Перевірка на турбо режим
    m_set_cmd.data.turbo = (get_cmd_resp->data.fan == 0x03) ? 1 : 0;
    m_set_cmd.data.mute = 0;

    // ПЕРЕДАЄМО РЕЖИМ ТА ФАН НАПРЯМУ (БЕЗ MODE_MAP ТА FAN_MAP)
    m_set_cmd.data.mode = get_cmd_resp->data.mode;
    m_set_cmd.data.fan = get_cmd_resp->data.fan;

    // Розрахунок температури для відправки (інвертована логіка кондиціонера)
    m_set_cmd.data.temp = 15 - get_cmd_resp->data.temp;

    m_set_cmd.data.vswing = 0;
    m_set_cmd.data.hswing = 0;
    m_set_cmd.data.half_degree = 0;

    // Рахуємо контрольну суму XOR
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

    // 1. ВИЗНАЧАЄМО РЕЖИМ (Завжди беремо поточний актуальний режим з Home Assistant)
    climate::ClimateMode active_mode = this->mode; 
    if (call.get_mode().has_value()) {
        active_mode = *call.get_mode(); // Якщо користувач клацнув новий режим — беремо його
    }

    // 2. КОДУЄМО ЖИВЛЕННЯ ТА РЕЖИМ НАПРЯМУ
    if (active_mode == climate::CLIMATE_MODE_OFF) {
        get_cmd_resp.data.power = 0x02; // Код вимкнення живлення
        get_cmd_resp.data.mode = 0x00;  
    } else {
        get_cmd_resp.data.power = 0x03; // Код увімкнення живлення
        
        // Твоя фінальна перевірена залізна карта кодів
        switch (active_mode) {
            case climate::CLIMATE_MODE_HEAT:     get_cmd_resp.data.mode = 0x01; break; // HEAT = 01
            case climate::CLIMATE_MODE_DRY:      get_cmd_resp.data.mode = 0x02; break; // DRY  = 02
            case climate::CLIMATE_MODE_COOL:     get_cmd_resp.data.mode = 0x03; break; // COOL = 03
            case climate::CLIMATE_MODE_FAN_ONLY: get_cmd_resp.data.mode = 0x07; break; // FAN  = 07 (Знайдено! 🎉)
            case climate::CLIMATE_MODE_AUTO:     get_cmd_resp.data.mode = 0x08; break; // AUTO = 08 (Знайдено! 🎉)
            default:                             get_cmd_resp.data.mode = 0x03; break; 
        }
    }
    should_build_cmd = true;

    // 3. ОБРОБКА ТЕМПЕРАТУРИ
    if (call.get_target_temperature().has_value()) {
        float temp = *call.get_target_temperature();
        get_cmd_resp.data.temp = static_cast<uint8_t>(temp) - 16;
    } else {
        // Якщо міняється режим, а не температура — беремо поточну виставлену в HA температуру
        get_cmd_resp.data.temp = static_cast<uint8_t>(this->target_temperature) - 16;
    }

    // 4. ЗБИРАЄМО СИРИЙ ПАКЕТ
    if (should_build_cmd) {
        build_set_cmd(&get_cmd_resp);
        
        // Жорстко записуємо правильний байт режиму в масив відправки, обнуляючи сміття
        m_set_cmd.raw[5] = (m_set_cmd.raw[5] & 0xF0) | (get_cmd_resp.data.mode & 0x0F);

        // Перераховуємо XOR контрольної суми
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

                // 1. Розбір стану живлення та режимів
                if (low_nibble == 0x00) {
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

                // ЧИТАЄМО 8-Й БАЙТ
                uint8_t byte8 = m_get_cmd_resp.raw[8];
                uint8_t fan_raw = (byte8 & 0xF0) >> 4; 
                uint8_t temp_raw = (byte8 & 0x0F);     

                // 2. Розбір швидкості вентилятора (01=Шв.1, 02=Шв.2, 03=Шв.3/Турбо, 00=Авто)
                switch (fan_raw) {
                    case 0x01: this->set_custom_fan_mode(StringRef("1")); break;
                    case 0x02: this->set_custom_fan_mode(StringRef("2")); break;
                    case 0x03: this->set_custom_fan_mode(StringRef("3")); break; 
                    case 0x00: this->set_custom_fan_mode(StringRef("Automatic")); break;
                    default:   this->set_custom_fan_mode(StringRef("Automatic")); break;
                }

                // 3. Розбір Цільової температури
                float target_t = temp_raw + 16;
                this->set_target_temperature(target_t);

                // Датчик поточної температури
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
