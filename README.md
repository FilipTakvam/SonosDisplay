# Sonos Display

An ESP32-based display system that shows album art and playback information from your Sonos speaker. The project uses a 64x64 RGB LED matrix panel to display real-time album artwork with smooth slide animations.

## Features

- **Album Art Display**: Shows current album artwork on a 64x64 RGB matrix display
- **Smooth Animations**: Slide in/out animations when the track changes
- **Brightness Control**: Rotary encoder for adjustable display brightness
- **WiFi Configuration**: Easy web-based WiFi setup interface
- **Configurable Speaker**: Select which Sonos speaker to track
- **Device Discovery**: Automatic retry logic for finding the Sonos speaker on the network

## Software Requirements

- **ESP-IDF v5.3.5** or compatible
- **Python 3.8+** (for ESP-IDF tools)
- A modern web browser (for WiFi configuration)

## Building the Project

### 1. Install ESP-IDF

Follow the [official ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) for your operating system.

### 2. Clone/Setup the Project

```bash
cd your/project/path
idf.py menuconfig  # Optional: configure project settings
```

### 3. Build the Project

```bash
idf.py build
```

### 4. Flash to ESP32

Connect your ESP32 via USB and flash:

```bash
idf.py -p PORT flash
```

Replace `PORT` with your actual serial port. On Linux/Mac, use `/dev/ttyUSB0` or similar.

### 5. Monitor Serial Output

```bash
idf.py -p PORT monitor
```

## Initial Setup

### First Time Configuration

1. **Connect Power**: Power on the ESP32 with the device
2. **Enter WiFi Config Mode**: Hold the encoder button during startup (within 20ms of power-on)
3. **Connect to AP**: Your device will create a WiFi access point named `SonosDisplay`
4. **Open Configuration Page**: 
   - On your phone/computer, connect to the `SonosDisplay` WiFi network
   - Open a web browser and navigate to `http://sonosdisplay.local/` (or `http://192.168.4.1/`)
5. **Fill in the Form**:
   - **Network name (SSID)**: Your home WiFi network name
   - **Password**: Your WiFi password
   - **Sonos Speaker Name**: The exact name of your Sonos speaker to display
6. **Save & Reboot**: Click "Save & Reboot" - the device will restart with your settings

### Subsequent Boots

After initial configuration, the device will:
1. Connect to your configured WiFi network
2. Attempt to discover your Sonos speaker (with 3 retry attempts)
3. Subscribe to speaker notifications and begin displaying album art

## Key Components

### MatrixDisplay
Handles all display rendering, animations, and brightness control:
- Display initialization and I2S DMA configuration
- Album art compositing with vertical slide animations
- Brightness control via rotary encoder
- 16ms refresh rate for smooth animation

### Wifi_Config
Web-based configuration interface:
- Creates an access point on first boot
- mDNS support for easy access via `sonosdisplay.local`
- Stores WiFi credentials and speaker name in NVS flash

### Sonos
Communication with Sonos speakers:
- SSDP device discovery
- SOAP control protocol
- Event subscriptions for real-time album art updates
- Album art download and caching

## Configuration Files

- **NVS Storage**: Stores WiFi SSID, password, and Sonos speaker name
- **Namespace**: `wifi_config`
- **Keys**: `ssid`, `password`, `sonos_name`

To reset configuration, erase NVS flash:
```bash
idf.py -p COM3 erase_flash
```
## License

This project includes components with their own licenses:
- ESP32-HUB75-MatrixPanel-I2S-DMA: See component LICENSE
- tinyxml2: zlib license
- Other components: See respective LICENSE files