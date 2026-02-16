# Flexispot E7 ESPHome Integration

ESPHome custom component for integrating Flexispot E7 standing desks with Home Assistant.

## Features

- Real-time desk height monitoring
- Preset buttons (Sit, Stand, 1, 2, Memory)
- Manual up/down control
- Automatic display wake-up to maintain height readings

## Hardware Requirements

- ESP32 development board
- Flexispot E7 desk with RJ45 control panel connection
- RJ45 breakout connector or cable

## Wiring

Connect the ESP32 to the desk's RJ45 port (directly from desk, not passing through handset):

| RJ45 Pin | Wire Color | ESP32 Pin | Function |
|----------|------------|-----------|----------|
| 1        | Green      | GPIO1     | TX (commands to desk) |
| 2        | White/Blue | GPIO3     | RX (data from desk) |
| 3        | Blue       | GPIO21    | Virtual screen enable |
| 8        | Black      | GND       | Ground |
| 8        | Red        | 5V/VIN    | Power (5V) |

> **Note:** Pin numbers may vary. Verify with a multimeter before connecting.

## Installation

1. Copy the `components/desk_height` folder to your ESPHome config directory
2. Copy `flexispot-e7.yaml` to your ESPHome config directory
3. Create `secrets.yaml` with your WiFi credentials:
   ```yaml
   wifi_ssid: "your_ssid"
   wifi_password: "your_password"
   ```
4. Flash to your ESP32:
   ```bash
   esphome run flexispot-e7.yaml
   ```

## Configuration

### Sensor Filters

The default filter removes zero values:

```yaml
sensor:
  - platform: desk_height
    filters:
      - filter_out: 0.0
```

Optional filters you can add:

```yaml
    filters:
      - filter_out: 0.0
      - throttle: 1s          # Limit updates to once per second
      - clamp:
          min_value: 60.0
          max_value: 125.0
```

### Polling Behavior

The component uses a three-state system:

1. **Boot Wait:** 5 seconds after startup, sends M command to get initial height
2. **Idle:** Polls every 3 seconds to detect changes
3. **Active:** When height changes, rapid polls every 300ms until movement stops

This ensures you always have current height while minimizing unnecessary traffic.

## Protocol

The desk uses a simple UART protocol at 9600 baud:

- **Start byte:** `0x9b` or `0x98`
- **Length byte:** payload length
- **Message type:** `0x12` for height data, `0x11` for heartbeat
- **Payload:** 7-segment display data (3 digits)
- **End byte:** `0x9d`

Height is transmitted as three 7-segment encoded bytes representing the display digits.

## Troubleshooting

### "Unknown" height in Home Assistant
- Check wiring connections
- Verify the virtual screen pin (GPIO21) is connected
- Check logs for UART communication

### Height not updating
- Ensure wake interval is running (check logs for "Wake Up" button presses)
- Verify clamp filter isn't rejecting valid heights

### Compile errors
- For faster compilation, switch from `esp-idf` to `arduino` framework in the YAML

## License

MIT License - See [LICENSE](LICENSE) file.

## Acknowledgments

Based on reverse engineering of the Flexispot UART protocol by the ESPHome community.
