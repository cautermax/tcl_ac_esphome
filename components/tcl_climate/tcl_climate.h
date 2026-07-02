#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace tcl_climate {

class TCLClimate : public climate::Climate, public uart::UARTDevice, public PollingComponent {
 public:
  TCLClimate(uart::UARTComponent *parent) : UARTDevice(parent) {}
  TCLClimate() : PollingComponent() {}

  bool is_changed : 1;
  std::string hswing_pos = "";
  std::string vswing_pos = "";

  // Перероблено під Новий 21-байтний протокол TCL/Ballu
  union get_cmd_resp_t {
    struct {
      uint8_t header;     // 0xBB
      uint8_t byte_1;     // 0x01
      uint8_t byte_2;     // 0x00
      uint8_t type;       // 0x03
      uint8_t len;        // 0x0F
      uint8_t byte_5;     // 0x01
      uint8_t byte_6;     // 0x00
      
      // Байт 7: Стан та Режим
      uint8_t mode : 4;   // Молодші 4 біти: 1 - cool, 4 - heat
      uint8_t power : 4;  // Старші 4 біти: 3 - ON, 2 - OFF

      // Байт 8: Температура та Вентилятор
      uint8_t temp : 4;   // Молодші 4 біти: температура (+16)
      uint8_t fan : 4;    // Старші 4 біти: швидкість вентилятора

      uint8_t byte_9;
      uint8_t byte_10;
      uint8_t byte_11;
      uint8_t byte_12;
      uint8_t byte_13;
      uint8_t byte_14;
      uint8_t byte_15;
      uint8_t byte_16;
      uint8_t byte_17;
      uint8_t byte_18;
      uint8_t byte_19;
      uint8_t byte_20;
      
      uint8_t xor_sum;    // Контрольна сума в кінці 21-го байту
    } data;
    uint8_t raw[21];      // Тепер довжина пакету відповіді чітко 21 байт
  };

  union set_cmd_t {
    struct {
      uint8_t header;
      uint8_t byte_1;
      uint8_t byte_2;
      uint8_t type;
      uint8_t len;
      uint8_t byte_5;
      uint8_t byte_6;
      
      uint8_t byte_7_bit_0_1 : 2;
      uint8_t power : 1;
      uint8_t off_timer_en : 1;
      uint8_t on_timer_en : 1;
      uint8_t beep : 1;
      uint8_t disp : 1;
      uint8_t eco : 1;

      uint8_t mode : 4;
      uint8_t byte_8_bit_4_5 : 2;
      uint8_t turbo : 1;
      uint8_t mute : 1;

      uint8_t temp : 4;
      uint8_t byte_9_bit_4_7 : 4;

      uint8_t fan : 3;
      uint8_t vswing : 3;
      uint8_t byte_10_bit_6 : 1;
      uint8_t byte_10_bit_7 : 1;

      uint8_t byte_11_bit_0_2 : 3;
      uint8_t hswing : 1;
      uint8_t byte_11_bit_4_7 : 4;

      uint8_t byte_12;
      uint8_t byte_13;

      uint8_t byte_14_bit_0_2 : 3;
      uint8_t byte_14_bit_3 : 1;
      uint8_t byte_14_bit_4 : 1;
      uint8_t half_degree : 1;
      uint8_t byte_14_bit_6_7 : 2;

      uint8_t byte_15;
      uint8_t byte_16;
      uint8_t byte_17;
      uint8_t byte_18;
      uint8_t byte_19;

      uint8_t byte_20;
      uint8_t byte_21;
      uint8_t byte_22;
      uint8_t byte_23;
      uint8_t byte_24;
      uint8_t byte_25;
      uint8_t byte_26;
      uint8_t byte_27;
      uint8_t byte_28;
      uint8_t byte_29;

      uint8_t byte_30;
      uint8_t byte_31;
      uint8_t vswing_fix : 3;
      uint8_t vswing_mv : 2;
      uint8_t byte_32_bit_5_7 : 3;

      uint8_t hswing_fix : 3;
      uint8_t hswing_mv : 3;
      uint8_t byte_33_bit_6_7 : 2;

      uint8_t xor_sum;
    } data;
    uint8_t raw[35];
  };

  bool ready_to_send_set_cmd_flag = false;

  uint8_t set_cmd_base[35] = {0xBB, 0x00, 0x01, 0x03, 0x1D, 0x00, 0x00, 0x64, 0x03, 0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  get_cmd_resp_t m_get_cmd_resp = {0};
  set_cmd_t m_set_cmd = {0};

  void set_current_temperature(float current_temperature);
  void set_custom_fan_mode(StringRef fan_mode);
  void set_mode(climate::ClimateMode mode);
  void set_swing_mode(climate::ClimateSwingMode swing_mode);
  void set_target_temperature(float target_temperature);

  void set_hswing_pos(const std::string &hswing_pos);
  void set_vswing_pos(const std::string &vswing_pos);
  void control_vertical_swing(const std::string &swing_mode);
  void control_horizontal_swing(const std::string &swing_mode);

  void build_set_cmd(get_cmd_resp_t *get_cmd_resp);

  void setup() override;
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;

  void update() override;
  void loop() override;

 private:
  int read_data_line(int readch, uint8_t *buffer, int len);
  bool is_valid_xor(uint8_t *buffer, int len);
  void print_hex_str(uint8_t *buffer, int len);
};

}  // namespace tcl_climate
}  // namespace esphome
