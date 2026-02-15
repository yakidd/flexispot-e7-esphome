# Flexispot E7 ESPHome Controller

Complete ESPHome integration for Flexispot E7 standing desks with continuous height monitoring and Home Assistant integration.

## Features

✅ **Real-time height monitoring** - Updates Home Assistant every 2 seconds  
✅ **Preset buttons** - Quick access to saved heights (1, 2, Sit, Stand)  
✅ **Manual controls** - Up/Down buttons  
✅ **Cover integration** - Nice slider UI in Home Assistant  
✅ **Memory function** - Save custom presets  
✅ **Safety limits** - Configurable min/max heights  
✅ **Auto-reconnect** - Periodic wake commands keep connection alive  
✅ **Web interface** - Direct access to ESP32  

## Hardware Requirements

- **ESP32 Development Board** (ESP32-DevKitC or similar)
- **Flexispot E7 Desk** with controller
- **Wires** for UART connection

## Wiring Diagram

```
ESP32          Desk Controller
-----          ---------------
GPIO1 (TX)  -> Green wire
GPIO3 (RX)  -> White/Blue wire
GPIO21      -> Blue wire (Virtual Screen)
GND         -> Ground
```

**Important:** The RJ45 connector on the desk uses the following pinout:
- Pin 1: Green (TX from desk)
- Pin 2: White/Blue (RX to desk)
- Pin 7: Blue (Screen power)
- Pin 8: Ground

## Software Requirements

- ESPHome 2026.1.5 or newer
- Home Assistant (for integration)

## Installation

### 1. Copy Files

Place these files in your ESPHome config directory:

```
esphome/
├── flexispot-e7.yaml
├── flexispot-e7-height-sensor.h
└── secrets.yaml
```

### 2. Configure Secrets

Edit `secrets.yaml` with your credentials:

```yaml
wifi_ssid: "YourWiFiNetwork"
wifi_password: "YourWiFiPassword"
fallback_password: "FallbackAPPassword"
api_encryption_key: ""  # Will be generated on first run
ota_password: "OTAPassword"
```

### 3. Adjust Settings

In `flexispot-e7.yaml`, update these values for your desk:

```yaml
substitutions:
  # Change these to match your desk's min/max heights
  min_height: "73.6"  # Your desk's minimum height in cm
  max_height: "122.9" # Your desk's maximum height in cm
  
  # Pin assignments (if different from defaults)
  uart_tx_pin: "GPIO1"
  uart_rx_pin: "GPIO3"
  screen_pin: "GPIO21"
```

### 4. Compile and Upload

```bash
# First time (USB connected)
esphome run flexispot-e7.yaml

# Subsequent updates (OTA)
esphome run flexispot-e7.yaml --device flexispot-e7.local
```

### 5. Add to Home Assistant

The device should auto-discover in Home Assistant. If not:
1. Go to **Settings** > **Devices & Services**
2. Click **Add Integration**
3. Search for **ESPHome**
4. Enter the device's IP address

## Usage

### Home Assistant Entities

After setup, you'll see these entities:

#### Sensors
- `sensor.desk_height` - Current desk height in cm
- `sensor.wifi_signal` - WiFi signal strength
- `sensor.uptime` - Device uptime

#### Buttons
- `button.preset_1` - Move to preset 1
- `button.preset_2` - Move to preset 2
- `button.sit` - Move to sitting position
- `button.stand` - Move to standing position
- `button.memory_m` - Open memory mode
- `button.restart` - Restart ESP32

#### Cover
- `cover.desk` - Slider control (0% = sitting, 100% = standing)

#### Numbers
- `number.target_height` - Set target height (manual entry)

### Setting Presets

To save a preset:
1. Move desk to desired height using up/down
2. Press `button.memory_m`
3. Press the preset button you want to save to (1, 2, Sit, or Stand)

### Automations Example

```yaml
# Morning routine - raise desk at 9 AM
automation:
  - alias: "Raise Desk Morning"
    trigger:
      - platform: time
        at: "09:00:00"
    action:
      - service: button.press
        target:
          entity_id: button.flexispot_e7_stand

# Lower desk after 8 hours
  - alias: "Lower Desk Evening"
    trigger:
      - platform: time
        at: "17:00:00"
    action:
      - service: button.press
        target:
          entity_id: button.flexispot_e7_sit
```

## Troubleshooting

### No height readings

**Check:**
- UART wiring (TX/RX may be swapped)
- Virtual screen is enabled (GPIO21 should be HIGH)
- Logs in ESPHome dashboard

**Try:**
```bash
esphome logs flexispot-e7.yaml
```

Look for `Height decoded: X.X cm` messages.

### Desk doesn't respond to buttons

**Check:**
- TX pin connection (green wire)
- UART baud rate (should be 9600)
- Desk is powered on

**Debug:**
Enable UART debugging in the YAML (already enabled by default):
```yaml
uart:
  debug:
    direction: BOTH
```

### Height stuck at zero

This usually means:
- Desk display is off/blank
- RX pin not connected properly
- Wrong baud rate

Press any physical button on the desk to wake it up.

### WiFi connection issues

If the device won't connect to WiFi:
1. Look for fallback AP: `Flexispot E7 Fallback`
2. Connect to it with password from `secrets.yaml`
3. Configure WiFi through captive portal

## Technical Details

### Protocol

The desk communicates via UART at 9600 baud using this packet structure:

```
[START] [LENGTH] [TYPE] [DATA...] [CHECKSUM] [END]
 0x9b     0x07     0x12   3 bytes    2 bytes   0x9d
```

**Message Types:**
- `0x11` - Heartbeat (ignored)
- `0x12` - Height broadcast (7 bytes data)

**Height Encoding:**
The 3 data bytes are 7-segment display values:
- Byte 1: Hundreds digit
- Byte 2: Tens digit (bit 7 = decimal point)
- Byte 3: Ones digit

Example: `[0x01] [0x8F] [0x06]` = 119.6 cm

### Continuous Updates

The `.h` file implements two update mechanisms:

1. **Immediate:** When desk broadcasts height (moving/button press)
2. **Periodic:** Publishes last known height every 2 seconds

This ensures Home Assistant always has current state.

### Safety Features

- Min/max height clamping prevents out-of-range values
- Delta filter (0.1cm) reduces noise
- Blank display detection ignores invalid readings
- Watchdog wake commands every 30s keep connection alive

## Customization

### Change update frequency

In `flexispot-e7-height-sensor.h`, modify:
```cpp
const unsigned long publish_interval_ = 2000; // milliseconds
```

### Adjust wake interval

In `flexispot-e7.yaml`:
```yaml
interval:
  - interval: 30s  # Change to 60s, 15s, etc.
    then:
      - button.press: button_wake
```

### Disable web server

Comment out in YAML:
```yaml
# web_server:
#   port: 80
```

## Credits

Original protocol reverse engineering by the ESPHome community.
Rewritten and enhanced for ESPHome 2026.1.5 with continuous monitoring.

## License

MIT License - Free to use and modify.

## Support

For issues or questions:
1. Check ESPHome logs: `esphome logs flexispot-e7.yaml`
2. Verify wiring with a multimeter
3. Test UART communication with a logic analyzer
4. Check ESPHome community forums

## Version History

- **v2.0** (2026-01-15) - Complete rewrite
  - Continuous height monitoring
  - Improved 7-segment decoding
  - Better error handling
  - Cover integration
  - Modern ESPHome 2026.1.5 compatibility

- **v1.0** - Original implementation
