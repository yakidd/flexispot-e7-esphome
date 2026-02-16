#include "desk_height_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace desk_height {

static const char *const TAG = "desk_height";

// M command - wakes display AND returns height
const uint8_t DeskHeightSensor::M_COMMAND[8] = {0x9b, 0x06, 0x02, 0x20, 0x00, 0xac, 0xb8, 0x9d};

void DeskHeightSensor::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Desk Height Sensor...");
  
  boot_time_ = millis();
  display_state_ = DisplayState::BOOT_WAIT;
  initial_reading_done_ = false;
}

void DeskHeightSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "Desk Height Sensor:");
  LOG_SENSOR("  ", "Desk Height", this);
}

float DeskHeightSensor::get_setup_priority() const {
  return setup_priority::DATA;
}

void DeskHeightSensor::loop() {
  uint32_t now = millis();
  
  // Read and process incoming UART data
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    
    if (buffer_index_ == 0) {
      // Wait for start byte
      if (byte == 0x9b || byte == 0x98) {
        buffer_[buffer_index_++] = byte;
      }
    } else {
      // Build the packet
      buffer_[buffer_index_++] = byte;
      
      // Check if we have enough bytes to know the expected length
      if (buffer_index_ >= 2) {
        int expected_length = buffer_[1] + 2; // payload + start + end
        
        // Got complete packet?
        if (buffer_index_ == expected_length) {
          // Validate end byte
          if (buffer_[expected_length - 1] == 0x9d) {
            process_packet_();
          } else {
            ESP_LOGW(TAG, "Invalid end byte: 0x%02X", buffer_[expected_length - 1]);
          }
          reset_buffer_();
        }
      }
      
      // Prevent buffer overflow
      if (buffer_index_ >= sizeof(buffer_)) {
        ESP_LOGW(TAG, "Buffer overflow, resetting");
        reset_buffer_();
      }
    }
  }

  // State machine
  switch (display_state_) {
    case DisplayState::BOOT_WAIT:
      // Wait 5 seconds after boot, then send initial M command
      if (now - boot_time_ >= BOOT_DELAY) {
        ESP_LOGI(TAG, "Sending initial M command to get desk height");
        send_m_command_();
        last_poll_time_ = now;
        display_state_ = DisplayState::IDLE;
      }
      break;
      
    case DisplayState::IDLE:
      // Check every 5 seconds for any incoming UART data
      if (now - last_poll_time_ >= IDLE_POLL_INTERVAL) {
        last_poll_time_ = now;
      }
      break;
      
    case DisplayState::ACTIVE:
      // Rapid poll every 300ms during movement
      if (now - last_poll_time_ >= ACTIVE_POLL_INTERVAL) {
        ESP_LOGD(TAG, "Polling for height (active)");
        send_m_command_();
        last_poll_time_ = now;
      }
      
      // Check if we should go back to idle
      if (now - last_activity_time_ >= ACTIVITY_TIMEOUT) {
        ESP_LOGI(TAG, "Desk stopped, slowing poll rate");
        display_state_ = DisplayState::IDLE;
      }
      break;
  }
}

void DeskHeightSensor::reset_buffer_() {
  buffer_index_ = 0;
  memset(buffer_, 0, sizeof(buffer_));
}

int DeskHeightSensor::decode_7segment_(uint8_t byte) {
  // Strip the decimal point bit
  uint8_t segments = byte & 0x7F;
  
  // Handle blank/off segment
  if (segments == 0x00) {
    return -2;  // Special value for blank segment
  }
  
  // Lookup table for 7-segment patterns
  const uint8_t patterns[11] = {
    0b00111111,  // 0
    0b00000110,  // 1
    0b01011011,  // 2
    0b01001111,  // 3
    0b01100110,  // 4
    0b01101101,  // 5
    0b01111101,  // 6
    0b00000111,  // 7
    0b01111111,  // 8
    0b01101111,  // 9
    0b01000000   // 10 (minus sign)
  };
  
  // Find matching pattern
  for (int i = 0; i < 11; i++) {
    if (segments == patterns[i]) {
      return i;
    }
  }
  
  ESP_LOGD(TAG, "Unknown 7-segment pattern: 0x%02X", segments);
  return -1;
}

bool DeskHeightSensor::has_decimal_(uint8_t byte) {
  return (byte & 0x80) == 0x80;
}

void DeskHeightSensor::process_packet_() {
  uint8_t msg_type = buffer_[2];
  uint8_t msg_length = buffer_[1];
  
  // Message type 0x12 = height broadcast (7 bytes total payload)
  if (msg_type == 0x12 && msg_length == 7) {
    // Extract the three 7-segment display bytes
    uint8_t digit1 = buffer_[3];  // Hundreds place
    uint8_t digit2 = buffer_[4];  // Tens place
    uint8_t digit3 = buffer_[5];  // Ones place
    
    // Decode each digit
    int d1 = decode_7segment_(digit1);
    int d2 = decode_7segment_(digit2);
    int d3 = decode_7segment_(digit3);
    
    // Check for fully blank display (all segments off = waking up)
    if (d1 == -2 && d2 == -2 && d3 == -2) {
      ESP_LOGD(TAG, "Display waking up (blank), ignoring");
      return;
    }
    
    // Any non-blank response = display is active, mark activity
    last_activity_time_ = millis();
    
    // Treat leading blank as zero (normal for heights < 100 cm)
    if (d1 == -2) {
      d1 = 0;
    }
    
    // Validate digits (unknown patterns or unexpected blanks)
    if (d1 < 0 || d2 < 0 || d3 < 0) {
      ESP_LOGD(TAG, "Display showing non-height data (d1=%d, d2=%d, d3=%d)", d1, d2, d3);
      return;
    }
    
    // Check for blank display (all zeros)
    if (d1 == 0 && d2 == 0 && d3 == 0) {
      ESP_LOGD(TAG, "Blank display, ignoring");
      return;
    }
    
    // Check for minus sign (desk resetting)
    if (d2 == 10) {
      ESP_LOGD(TAG, "Desk showing minus sign (resetting)");
      return;
    }
    
    // Calculate height
    int height_raw = (d1 * 100) + (d2 * 10) + d3;
    float new_height;
    
    // Check if decimal point is set (divide by 10)
    if (has_decimal_(digit2)) {
      new_height = height_raw / 10.0f;
    } else {
      new_height = static_cast<float>(height_raw);
    }
    
    ESP_LOGD(TAG, "Height decoded: %.1f cm", new_height);
    
    // Check for height change - switch to active mode
    if (current_height_ > 0 && new_height != current_height_ && display_state_ == DisplayState::IDLE) {
      ESP_LOGI(TAG, "Height change detected, increasing poll rate");
      display_state_ = DisplayState::ACTIVE;
    }
    
    current_height_ = new_height;
    
    // Publish height
    publish_current_height_();
  }
  // Message type 0x11 = heartbeat (ignore silently)
  else if (msg_type == 0x11) {
    // Heartbeat packet, ignore
  }
  else {
    ESP_LOGV(TAG, "Unknown message type: 0x%02X (length: %d)", msg_type, msg_length);
  }
}

void DeskHeightSensor::publish_current_height_() {
  // Only publish if we have a valid height and it has changed
  if (current_height_ > 0.0f && current_height_ != last_published_height_) {
    this->publish_state(current_height_);
    last_published_height_ = current_height_;
    ESP_LOGI(TAG, "Published height: %.1f cm", current_height_);
  }
}

void DeskHeightSensor::send_m_command_() {
  this->write_array(M_COMMAND, sizeof(M_COMMAND));
}

}  // namespace desk_height
}  // namespace esphome
