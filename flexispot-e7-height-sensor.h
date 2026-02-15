#include "esphome.h"

/**
 * DeskHeightSensor - ESPHome custom sensor for Flexispot E7 standing desk
 * 
 * This component monitors UART communication from the desk controller and
 * decodes the current desk height from 7-segment display values.
 * 
 * Protocol:
 * - Start byte: 0x9b or 0x98
 * - Length byte: payload length
 * - Message type: 0x12 for height broadcasts
 * - Data: 3 bytes representing 7-segment encoded digits
 * - End byte: 0x9d
 */
class DeskHeightSensor : public Component, public UARTDevice, public Sensor {
public:
  DeskHeightSensor(UARTComponent *parent) : UARTDevice(parent) {}

  void setup() override {
    ESP_LOGI("desk", "Desk Height Sensor initialized");
  }

  float get_setup_priority() const override { 
    return esphome::setup_priority::DATA; 
  }

  void loop() override {
    // Read and process incoming UART data
    while (available()) {
      uint8_t byte = read();
      
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
              process_packet();
            } else {
              ESP_LOGW("desk", "Invalid end byte: 0x%02X", buffer_[expected_length - 1]);
            }
            reset_buffer();
          }
        }
        
        // Prevent buffer overflow
        if (buffer_index_ >= sizeof(buffer_)) {
          ESP_LOGW("desk", "Buffer overflow, resetting");
          reset_buffer();
        }
      }
    }

    // Publish current height periodically
    unsigned long now = millis();
    if (now - last_publish_time_ >= publish_interval_) {
      publish_current_height();
      last_publish_time_ = now;
    }
  }

private:
  // Buffer for incoming UART data
  uint8_t buffer_[32];
  size_t buffer_index_ = 0;
  
  // Current desk height in cm
  float current_height_ = 0.0;
  float last_published_height_ = -1.0;
  
  // Timing for periodic publishing
  unsigned long last_publish_time_ = 0;
  const unsigned long publish_interval_ = 2000; // Publish every 2 seconds
  
  /**
   * Reset the receive buffer
   */
  void reset_buffer() {
    buffer_index_ = 0;
    memset(buffer_, 0, sizeof(buffer_));
  }
  
  /**
   * Decode a 7-segment display byte to a digit (0-9)
   * 
   * Each bit represents a segment:
   *   _6_
   *  |   |
   *  5   1
   *  |_0_|
   *  |   |
   *  4   2
   *  |_3_|
   * 
   * Bit 7 (0x80) indicates decimal point
   */
  int decode_7segment(uint8_t byte) {
    // Strip the decimal point bit
    uint8_t segments = byte & 0x7F;
    
    // Lookup table for 7-segment patterns
    // Each pattern is: segments 0-6 (bit positions 0-6)
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
      0b01000000   // 10 (minus sign / blank)
    };
    
    // Find matching pattern
    for (int i = 0; i < 11; i++) {
      if (segments == patterns[i]) {
        return i;
      }
    }
    
    ESP_LOGW("desk", "Unknown 7-segment pattern: 0x%02X", segments);
    return -1;
  }
  
  /**
   * Check if a byte has the decimal point bit set
   */
  bool has_decimal(uint8_t byte) {
    return (byte & 0x80) == 0x80;
  }
  
  /**
   * Process a complete packet from the desk controller
   */
  void process_packet() {
    uint8_t msg_type = buffer_[2];
    uint8_t msg_length = buffer_[1];
    
    // Message type 0x12 = height broadcast (7 bytes total payload)
    if (msg_type == 0x12 && msg_length == 7) {
      // Extract the three 7-segment display bytes
      uint8_t digit1 = buffer_[3];  // Hundreds place
      uint8_t digit2 = buffer_[4];  // Tens place
      uint8_t digit3 = buffer_[5];  // Ones place
      
      // Decode each digit
      int d1 = decode_7segment(digit1);
      int d2 = decode_7segment(digit2);
      int d3 = decode_7segment(digit3);
      
      // Validate digits
      if (d1 < 0 || d2 < 0 || d3 < 0) {
        ESP_LOGW("desk", "Invalid digit in height packet");
        return;
      }
      
      // Check for blank display (all zeros or invalid)
      if (d1 == 0 && d2 == 0 && d3 == 0) {
        ESP_LOGD("desk", "Blank display, ignoring");
        return;
      }
      
      // Check for minus sign (desk resetting)
      if (d2 == 10) {
        ESP_LOGD("desk", "Desk showing minus sign (resetting)");
        return;
      }
      
      // Calculate height
      int height_raw = (d1 * 100) + (d2 * 10) + d3;
      
      // Check if decimal point is set (divide by 10)
      if (has_decimal(digit2)) {
        current_height_ = height_raw / 10.0;
      } else {
        current_height_ = height_raw;
      }
      
      ESP_LOGD("desk", "Height decoded: %.1f cm (raw: %d%s%d%d)", 
               current_height_, d1, has_decimal(digit2) ? "." : "", d2, d3);
      
      // Immediately publish when we receive an update
      publish_current_height();
    }
    // Message type 0x11 = heartbeat (ignore silently)
    else if (msg_type == 0x11) {
      // Heartbeat packet, ignore
    }
    else {
      ESP_LOGV("desk", "Unknown message type: 0x%02X (length: %d)", msg_type, msg_length);
    }
  }
  
  /**
   * Publish the current height to Home Assistant
   */
  void publish_current_height() {
    // Only publish if we have a valid height and it has changed
    if (current_height_ > 0.0 && current_height_ != last_published_height_) {
      publish_state(current_height_);
      last_published_height_ = current_height_;
      ESP_LOGI("desk", "Published height: %.1f cm", current_height_);
    }
  }
};
