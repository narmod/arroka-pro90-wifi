#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace arroka {

static const char *const TAG = "arroka";

class ArrokaClimate : public climate::Climate,
                      public Component,
                      public uart::UARTDevice {
 public:
  void set_de_re_pin(uint8_t pin) { de_re_pin_ = pin; }

  void setup() override {
    pinMode(de_re_pin_, OUTPUT);
    digitalWrite(de_re_pin_, LOW);
    this->current_temperature = NAN;
    this->target_temperature  = 28;
    this->mode   = climate::CLIMATE_MODE_HEAT;
    this->action = climate::CLIMATE_ACTION_IDLE;
    ESP_LOGI(TAG, "ArrokaClimate ready, DE/RE pin=%d", de_re_pin_);
  }

  climate::ClimateTraits traits() override {
    auto t = climate::ClimateTraits();
    t.set_supports_current_temperature(true);
    t.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_COOL,
    });
    t.set_visual_min_temperature(15);
    t.set_visual_max_temperature(32);
    t.set_visual_temperature_step(1);
    return t;
  }

  void control(const climate::ClimateCall &call) override {
    bool    new_on   = (this->mode != climate::CLIMATE_MODE_OFF);
    bool    new_heat = (this->mode != climate::CLIMATE_MODE_COOL);
    uint8_t new_sp   = (uint8_t) this->target_temperature;

    if (call.get_mode().has_value()) {
      switch (*call.get_mode()) {
        case climate::CLIMATE_MODE_OFF:
          new_on = false;
          break;
        case climate::CLIMATE_MODE_HEAT:
          new_on = true; new_heat = true;
          break;
        case climate::CLIMATE_MODE_COOL:
          new_on = true; new_heat = false;
          break;
        default:
          break;
      }
    }
    if (call.get_target_temperature().has_value())
      new_sp = (uint8_t) *call.get_target_temperature();

    pending_cmd_  = true;
    pending_on_   = new_on;
    pending_heat_ = new_heat;
    pending_sp_   = new_sp;

    ESP_LOGI(TAG, "Control: %s %s SP=%d",
      new_on ? "ON" : "OFF", new_heat ? "HEAT" : "COOL", new_sp);
  }

  void loop() override {
    while (available()) {
      buf_[buf_len_++] = read();
      last_byte_ms_ = millis();
      if (buf_len_ == 13) {
        process_frame(buf_);
        if (buf_[0] == 0xCC && pending_cmd_) {
          delay(3);
          send_command(pending_on_, pending_heat_, pending_sp_);
          pending_cmd_ = false;
        }
        buf_len_ = 0;
      }
    }
    if (buf_len_ > 0 && millis() - last_byte_ms_ > 20)
      buf_len_ = 0;
  }

 protected:
  uint8_t  de_re_pin_{4};
  uint8_t  last_cc_[13] = {
    0xCC,0x19,0x1C,0x2D,0x07,0x0D,0xA0,0x2C,0x19,0x02,0x00,0x7F,0xB9
  };
  bool     last_cc_valid_{false};
  bool     pending_cmd_{false};
  bool     pending_on_{false};
  bool     pending_heat_{true};
  uint8_t  pending_sp_{28};
  uint8_t  buf_[64];
  int      buf_len_{0};
  uint32_t last_byte_ms_{0};

  uint8_t mode_flag(bool on, bool heat) {
    return 0x0C | (on ? 0x40 : 0x00) | (heat ? 0x20 : 0x00);
  }

  void process_frame(uint8_t *d) {
    bool changed = false;

    if (d[0] == 0xDD) {
      float tw      = (float) d[1];
      bool  running = (d[8] != 0x00);

      if (tw != this->current_temperature) {
        this->current_temperature = tw;
        changed = true;
      }

      climate::ClimateAction new_action;
      if (!running)
        new_action = climate::CLIMATE_ACTION_IDLE;
      else if (this->mode == climate::CLIMATE_MODE_HEAT)
        new_action = climate::CLIMATE_ACTION_HEATING;
      else
        new_action = climate::CLIMATE_ACTION_COOLING;

      if (new_action != this->action) {
        this->action = new_action;
        changed = true;
      }
    }

    if (d[0] == 0xCC) {
      memcpy(last_cc_, d, 13);
      last_cc_valid_ = true;

      float sp   = (float) d[2];
      bool  on   = (d[7] & 0x40) != 0;
      bool  heat = (d[7] & 0x20) != 0;

      if (sp != this->target_temperature) {
        this->target_temperature = sp;
        changed = true;
      }

      climate::ClimateMode new_mode;
      if (!on)       new_mode = climate::CLIMATE_MODE_OFF;
      else if (heat) new_mode = climate::CLIMATE_MODE_HEAT;
      else           new_mode = climate::CLIMATE_MODE_COOL;

      if (new_mode != this->mode) {
        this->mode = new_mode;
        changed = true;
      }
    }

    if (changed) this->publish_state();
  }

  void send_command(bool on, bool heat, uint8_t sp) {
    if (!last_cc_valid_) {
      ESP_LOGW(TAG, "Pas de trame CC connue, commande annulee");
      return;
    }
    uint8_t frame[13];
    memcpy(frame, last_cc_, 13);
    frame[0]  = 0xCD;
    frame[2]  = sp;
    frame[7]  = mode_flag(on, heat);

    uint8_t x = 0;
    for (int i = 0; i < 12; i++) x ^= frame[i];
    frame[12] = x ^ 0xBD;

    digitalWrite(de_re_pin_, HIGH);
    delay(2);
    write_array(frame, 13);
    flush();
    delay(2);
    digitalWrite(de_re_pin_, LOW);

    ESP_LOGI(TAG, "TX: %02X %02X %02X CRC=%02X",
      frame[0], frame[2], frame[7], frame[12]);
  }
};

}  // namespace arroka
}  // namespace esphome
