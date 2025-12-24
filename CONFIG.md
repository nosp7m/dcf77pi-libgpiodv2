# Configuration Guide for dcf77pi

## Configuration File Location

The configuration file is `config.json`, located at:
- **Linux**: `/etc/dcf77pi/config.json` (when installed with `ETCDIR=/etc/dcf77pi`)
- **FreeBSD**: `/usr/local/etc/dcf77pi/config.json` (default)

## Configuration Parameters

### Required Parameters

#### `pin` (integer, 0-65535)
The BCM GPIO number to read the DCF77 signal from.

**Important:** This is the BCM GPIO number, NOT the physical pin number!

**How to find your GPIO number:**
```bash
# Use pinctrl to identify the GPIO number you're using
pinctrl get <number>

# Example: if your DCF77 is on physical pin 15, try:
pinctrl get 15    # Shows GPIO 15 status
# The number at the start of the output (e.g., "15: ...") is your GPIO number
```

Common mappings for Raspberry Pi (varies by model):
- Physical pin 11 → Usually GPIO 17
- Physical pin 13 → Usually GPIO 27  
- Physical pin 15 → Usually GPIO 22 (but can be GPIO 15 on some models)
- Physical pin 16 → Usually GPIO 23
- Physical pin 18 → Usually GPIO 24

**Always verify with `pinctrl` or check your specific Raspberry Pi pinout diagram!**

**Example:** `"pin": 15` if using GPIO 15

#### `activehigh` (boolean)
Whether the DCF77 signal is active-high or active-low.

- `true`: Signal is HIGH (1) when active, LOW (0) when inactive
- `false`: Signal is LOW (0) when active, HIGH (1) when inactive

Most DCF77 modules are **active-low** (signal is normally HIGH, drops LOW for pulses).

**Troubleshooting:** If you only see zeros or only see ones, try toggling this setting.

**Example:** `"activehigh": false`

#### `freq` (integer, 10-155000, must be even)
Sample frequency in Hz. This determines how often the GPIO pin is read per second.

- Higher values provide better precision but use more CPU
- Default: `1000` (read 1000 times per second = 1ms intervals)
- Recommended range: 600-2000

**Example:** `"freq": 1000`

### Platform-Specific Parameters

#### `iodev` (integer, FreeBSD only)
GPIO device number for FreeBSD systems.

**Example:** `"iodev": 0` (uses `/dev/gpioc0`)

#### `gpiochip` (string, Linux only)
Path to the GPIO chip device.

**Default:** `/dev/gpiochip0`

Most Raspberry Pi systems use `/dev/gpiochip0`. Older or custom systems might use different chip numbers.

**Example:** `"gpiochip": "/dev/gpiochip0"`

#### `bias` (string, Linux only, libgpiod v2)
GPIO line bias setting. Controls the internal pull-up/pull-down resistors.

**Valid values:**
- `"disabled"` *(default, recommended)*: No internal bias, let the DCF77 module control the line
- `"pull-up"`: Enable internal pull-up resistor (pulls line HIGH when floating)
- `"pull-down"`: Enable internal pull-down resistor (pulls line LOW when floating)

**When to use:**
- `"disabled"`: Most DCF77 modules have their own pull resistors (use this first)
- `"pull-up"`: Your module datasheet requires it, or you're using a bare DCF77 chip
- `"pull-down"`: Rarely needed, only if explicitly required by your hardware

**Troubleshooting:** If you see all zeros or unstable readings, try different bias settings.

**Example:** `"bias": "disabled"`

### Optional Parameters

#### `outlogfile` (string)
Path to the output log file for recording received bits.

- Empty string `""`: No logging (default)
- File path: Log all received bits to this file for later analysis with `dcf77pi-analyze`

**Example:** `"outlogfile": "/var/log/dcf77pi.log"`

**Note:** This parameter is only used by `dcf77pi` and `dcf77pi-analyze`. It is ignored by `dcf77pi-daemon` (which logs to systemd journal only).

#### `shm_unit` (integer, 0-3, dcf77pi-daemon only)
Shared memory unit number. Only used by the `dcf77pi-daemon` daemon.

- `0` *(default)*: Use SHM segment 0 (key 0x4e545030)
- `1-3`: Use SHM segments 1-3 for multiple time sources

This must match the `unit` number in your NTPsec/Chrony refclock configuration. For example:
- `"shm_unit": 0` corresponds to `refclock shm unit 0` in `/etc/ntpsec/ntp.conf`

**Example:** `"shm_unit": 0`

**Note:** This parameter is ignored by `dcf77pi` and `dcf77pi-analyze`.

## Example Configurations

### Basic Configuration (Raspberry Pi, physical pin 15)

```json
{
	"pin": 15,
	"iodev": 0,
	"gpiochip": "/dev/gpiochip0",
	"bias": "disabled",
	"activehigh": false,
	"freq": 1000,
	"outlogfile": ""
}
```

### With Logging Enabled

```json
{
	"pin": 15,
	"iodev": 0,
	"gpiochip": "/dev/gpiochip0",
	"bias": "disabled",
	"activehigh": false,
	"freq": 1000,
	"outlogfile": "/var/log/dcf77pi.log"
}
```

### For Module Requiring Pull-up

```json
{
	"pin": 15,
	"iodev": 0,
	"gpiochip": "/dev/gpiochip0",
	"bias": "pull-up",
	"activehigh": false,
	"freq": 1000,
	"outlogfile": ""
}
```

## Troubleshooting

### Only Seeing Zeros

1. Check `activehigh` setting - try toggling between `true` and `false`
2. Verify GPIO pin number is BCM numbering (not physical pin)
3. Test with `pinctrl get <gpio_num>` to see if signal is changing
4. Try different `bias` settings: `"disabled"`, `"pull-up"`, `"pull-down"`
5. Check DCF77 module is powered (3.3V, not 5V!)
6. Verify antenna connection and positioning

### Only Seeing Ones

1. Toggle `activehigh` setting
2. Check if DCF77 module is receiving signal (some modules have LED indicator)
3. Try different `bias` settings

### "Device or resource busy" Error

1. Verify correct GPIO pin number
2. **Check if the GPIO is claimed by sysfs or another process:**
   ```bash
   gpioinfo -c gpiochip0 | grep "consumer="
   ```
   If you see `consumer="sysfs"` or another consumer on your GPIO line, it's already in use.

3. **If claimed by old sysfs interface:**
   ```bash
   # Check if GPIO is exported
   ls /sys/class/gpio/
   
   # Unexport it (replace 15 with your GPIO number)
   echo 15 | sudo tee /sys/class/gpio/unexport
   
   # Or simply reboot to clear all claims
   sudo reboot
   ```

4. Check for conflicting device tree overlays in `/boot/config.txt` or `/boot/firmware/config.txt`

### No Signal Detected

1. DCF77 signal is very weak - ensure good antenna positioning
2. Move antenna away from electronics/interference sources
3. Wait for better reception (nighttime is often better)
4. Verify module is powered correctly
5. Increase `freq` for better precision (try 1500-2000)

## Testing Your Configuration

1. Test GPIO reading:
   ```bash
   dcf77pi-readpin -r
   ```
   You should see 0s and 1s changing (mostly 1s with brief 0s if active-low)

2. Monitor for valid DCF77 signal:
   ```bash
   dcf77pi-readpin
   ```
   Wait ~1 minute to see decoded bits

3. Full interactive mode:
   ```bash
   dcf77pi
   ```
   Watch the display for time synchronization
