# Migration to libgpiod v2

This document describes the migration from the deprecated sysfs GPIO interface (`/sys/class/gpio/`) to the modern libgpiod v2 library for GPIO access on Linux systems, particularly for Raspberry Pi OS Trixie and newer.

## Changes Made

### 1. Makefile
- Added `GPIOD_C` and `GPIOD_L` variables to include libgpiod compilation flags and library linking
- Uses `pkg-config` to automatically detect libgpiod installation
- Updated `input.o` compilation to include libgpiod flags
- Updated `libdcf77.so` linking to include libgpiod library

### 2. input.h
- Added `#include <gpiod.h>` for Linux systems
- Added `gpiochip` field to `struct hardware` for Linux to specify GPIO chip path (default: `/dev/gpiochip0`)

### 3. input.c
- Added static variables for libgpiod:
  - `struct gpiod_chip *chip` - GPIO chip handle
  - `struct gpiod_line_request *line_req` - GPIO line request handle

- **set_mode_live()**: Complete rewrite of Linux GPIO initialization
  - Replaced sysfs file operations with libgpiod v2 API calls:
    - `gpiod_chip_open()` - Open GPIO chip
    - `gpiod_line_settings_new()` - Create line settings
    - `gpiod_line_settings_set_direction()` - Set line as input
    - `gpiod_line_config_new()` - Create line configuration
    - `gpiod_line_config_add_line_settings()` - Add settings to configuration
    - `gpiod_request_config_new()` - Create request configuration
    - `gpiod_chip_request_lines()` - Request GPIO line access

- **get_pulse()**: Updated GPIO reading for Linux
  - Replaced `read()` from sysfs with `gpiod_line_request_get_value()`
  - Removed file descriptor seek operations (no longer needed)

- **cleanup()**: Updated resource cleanup for Linux
  - Replaced `close(fd)` with:
    - `gpiod_line_request_release()` - Release line request
    - `gpiod_chip_close()` - Close GPIO chip
  - FreeBSD cleanup remains unchanged

### 4. config.json
- Added new optional parameter `gpiochip` (Linux only)
- Default value: `/dev/gpiochip0`
- Allows users to specify alternative GPIO chips if needed

### 5. README.md
- Updated dependency installation instructions to include `libgpiod-dev`
- Added documentation for the new `gpiochip` configuration parameter
- Added note about libgpiod v2 requirement for Raspberry Pi OS Trixie and newer
- Clarified that `iodev` is FreeBSD-only, `gpiochip` is Linux-only

## Installation Requirements

### For Raspberry Pi OS Trixie and newer:
```sh
sudo apt-get install libncurses5-dev libjson-c-dev libgpiod-dev pkgconf
```

### For older Raspberry Pi OS versions:
You may need to install libgpiod v2 from source or upgrade to a newer OS version.

## Configuration

### Example config.json for Raspberry Pi (Linux):
```json
{
	"pin" : 17,
	"gpiochip" : "/dev/gpiochip0",
	"activehigh" : true,
	"freq" : 1000,
	"outlogfile" : ""
}
```

**Note:** The `iodev` parameter is ignored on Linux systems.

### Example config.json for FreeBSD:
```json
{
	"pin" : 17,
	"iodev" : 0,
	"activehigh" : true,
	"freq" : 1000,
	"outlogfile" : ""
}
```

**Note:** The `gpiochip` parameter is ignored on FreeBSD systems.

## Backward Compatibility

- FreeBSD GPIO access remains unchanged (still uses `/dev/gpioc*`)
- Linux systems now use libgpiod v2 instead of sysfs
- The configuration file format is backward compatible - old config files will work with default values for new parameters

## Benefits of libgpiod v2

1. **Modern API**: libgpiod is the recommended way to access GPIO on modern Linux systems
2. **Better Performance**: More efficient than sysfs file operations
3. **Improved Reliability**: Better error handling and resource management
4. **Future-Proof**: Sysfs GPIO interface is deprecated and may be removed in future kernels
5. **Character Device Interface**: Uses `/dev/gpiochip*` character devices instead of sysfs

## Testing

After installation, test the GPIO reading functionality:

```sh
# Test raw GPIO reading
dcf77pi-readpin -r

# Test with bit decoding
dcf77pi-readpin

# Run the full interactive decoder
dcf77pi
```

## Troubleshooting

### "gpiod_chip_open: No such file or directory"
- Check if `/dev/gpiochip0` exists
- Try `ls /dev/gpiochip*` to see available GPIO chips
- Update `gpiochip` parameter in config.json if needed

### "Permission denied" when accessing GPIO
- Add your user to the `gpio` group: `sudo usermod -a -G gpio $USER`
- Or run with sudo (not recommended for production)

### libgpiod not found during compilation
- Install libgpiod-dev: `sudo apt-get install libgpiod-dev`
- Ensure pkg-config is installed: `sudo apt-get install pkgconf`

## References

- libgpiod documentation: https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about/
- Raspberry Pi GPIO documentation: https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
