# dcf77pi-ntpsec - DCF77 NTPSec Daemon

## Overview

`dcf77pi-ntpsec` is a systemd daemon that decodes DCF77 time signals from a GPIO-connected receiver and provides the time to NTPSec via its shared memory (SHM) interface. This allows your Raspberry Pi to act as a stratum 1 NTP server using DCF77 as the reference clock.

## Features

- **Daemon mode**: Runs as a systemd service in the background
- **NTPSec SHM integration**: Provides time via NTPSec's shared memory interface
- **Systemd journal logging**: All output goes to the systemd journal for easy monitoring
- **Error logging**: Detailed error messages for decoding failures and hardware issues
- **Success logging**: Logs every successful time decode
- **Graceful shutdown**: Handles SIGINT and SIGTERM signals cleanly
- **No UI**: Lightweight, no ncurses dependency

## Building

Build the daemon along with other dcf77pi tools:

```bash
make
```

Or build just the daemon:

```bash
make dcf77pi-ntpsec
```

Install:

```bash
sudo make install
```

## Configuration

### 1. DCF77 Configuration

Edit `/etc/dcf77pi/config.json` (or `/usr/local/etc/dcf77pi/config.json`):

```json
{
    "pin": 15,
    "gpiochip": "/dev/gpiochip0",
    "bias": "disabled",
    "activehigh": false,
    "freq": 1000,
    "shm_unit": 0
}
```

**New parameter:**
- `shm_unit`: NTPSec SHM unit number (0-3, default 0). This corresponds to the SHM segment that NTPSec will read from. Use different numbers if you have multiple time sources.

**Note:** The `outlogfile` parameter is ignored by `dcf77pi-ntpsec`. All logging goes to systemd journal only.

### 2. NTPSec Configuration

#### Step 1: Edit the NTPSec configuration file

Add the DCF77 SHM refclock to your NTPSec configuration (`/etc/ntpsec/ntp.conf`):

```
# DCF77 via shared memory (SHM unit 0)
refclock shm unit 0 refid DCF77 prefer
# Adjust time1 offset if needed (in seconds)
# time1 0.0

# Optional: Set stratum if this is your primary time source
# tos minsane 1
```

If you changed `shm_unit` in config.json, make sure the `unit` number matches.

#### Step 2: Configure NTPSec to use the static configuration

Some systems (especially when using DHCP) configure NTPSec to use a dynamically generated config file (e.g., `/run/ntpsec/ntp.conf.dhcp`) instead of the static `/etc/ntpsec/ntp.conf`. To ensure NTPSec uses your static configuration:

**Check which config file is being used:**

```bash
systemctl status ntpsec | grep "Command line"
```

If you see `-c /run/ntpsec/ntp.conf.dhcp` or similar, you need to override this.

**Create a systemd service override:**

```bash
sudo systemctl edit ntpsec
```

Add these lines in the editor that opens:

```ini
[Service]
ExecStart=
ExecStart=/usr/sbin/ntpd -p /run/ntpd.pid -g -u ntpsec:ntpsec
```

The first `ExecStart=` clears the original command, and the second sets it to use the default `/etc/ntpsec/ntp.conf` (without the `-c` parameter).

Save and exit the editor (Ctrl+O, Enter, Ctrl+X in nano).

**Alternative manual method:**

```bash
# Create override directory
sudo mkdir -p /etc/systemd/system/ntpsec.service.d/

# Create override file
sudo nano /etc/systemd/system/ntpsec.service.d/override.conf
```

Add this content:

```ini
[Service]
ExecStart=
ExecStart=/usr/sbin/ntpd -p /run/ntpd.pid -g -u ntpsec:ntpsec
```

#### Step 3: Apply changes and restart NTPSec

```bash
# Reload systemd configuration
sudo systemctl daemon-reload

# Restart NTPSec
sudo systemctl restart ntpsec

# Verify it's using the correct config file
systemctl status ntpsec | grep "Command line"
```

You should now see the command line **without** the `-c` parameter, meaning it's using `/etc/ntpsec/ntp.conf`.

### 3. Permissions

The daemon needs access to:
- GPIO device (usually `/dev/gpiochip0`)
- Configuration file (`/etc/dcf77pi/config.json`)
- Optionally: Log file directory for raw bit logging

**Option A: Run as root (easier, less secure)**

The provided service file runs as root by default.

**Option B: Run as dedicated user (more secure)**

Create a user and grant GPIO access:

```bash
# Create dcf77pi user
sudo useradd -r -s /bin/false dcf77pi

# Add user to gpio group (if your system uses it)
sudo usermod -a -G gpio dcf77pi

# Or use udev rules to grant GPIO access
sudo nano /etc/udev/rules.d/99-gpio.rules
```

Example udev rule:
```
SUBSYSTEM=="gpio", GROUP="gpio", MODE="0660"
SUBSYSTEM=="gpiochip", GROUP="gpio", MODE="0660"
```

Then uncomment the `User=` and `Group=` lines in the service file.

## Installation as Service

**Note:** Running `sudo make install` automatically installs and enables the systemd service on Linux systems.

If you need to manually manage the service:

1. Copy the service file:

```bash
sudo cp dcf77pi-ntpsec.service /etc/systemd/system/
```

2. Reload systemd:

```bash
sudo systemctl daemon-reload
```

3. Enable the service to start at boot:

```bash
sudo systemctl enable dcf77pi-ntpsec.service
```

4. Start the service:

```bash
sudo systemctl start dcf77pi-ntpsec.service
```

## Monitoring

### View service status

```bash
sudo systemctl status dcf77pi-ntpsec.service
```

### View logs

```bash
# View all logs
journalctl -u dcf77pi-ntpsec.service

# Follow logs in real-time
journalctl -u dcf77pi-ntpsec.service -f

# View only today's logs
journalctl -u dcf77pi-ntpsec.service --since today

# View last 100 lines
journalctl -u dcf77pi-ntpsec.service -n 100
```

### Check NTPSec synchronization

```bash
# Check NTPSec peer status
ntpq -p

# Look for the DCF77 refclock (should show as a peer with refid "DCF77")
# Example output:
#      remote           refid      st t when poll reach   delay   offset  jitter
# ==============================================================================
# *SHM(0)          .DCF7.          0 l   11   16  377    0.000    0.123   0.045
```

**Understanding the output:**

- **`*` prefix** - Currently selected time source (synchronized)
- **`+` prefix** - Good candidate for synchronization
- **` ` (space)** - Valid but not selected
- **`-` prefix** - Discarded as unreliable
- **`st 0`** - Stratum 0 (atomic clock level, highest quality)
- **`reach`** - Octal value showing successful polls (377 = 8 successful polls in a row)
  - Initial value will be low (e.g., `1`, `3`, `7`) and increase over time
  - Full synchronization achieved when `reach` = 377
- **`offset`** - Time difference in milliseconds between your system and DCF77
- **`jitter`** - Variation in offset measurements

**Note:** After initial configuration, it may take 5-10 minutes for:
1. `reach` to increase from 0 to 377 as NTPSec collects samples
2. The `*` indicator to appear, showing DCF77 as the selected source
3. Full synchronization to be achieved

You can watch the synchronization progress in real-time:

```bash
watch -n 10 ntpq -p
```

### Check shared memory

```bash
# List IPC shared memory segments
ipcs -m

# Should show a segment with key 0x4e545030 (for unit 0)
# For unit 1: 0x4e545031, unit 2: 0x4e545032, etc.
```

## Log Messages

### Successful operation

```
[2024-12-23 10:30:45] INFO: dcf77pi-ntpsec daemon starting
[2024-12-23 10:30:45] INFO: Initializing NTPSec SHM unit 0 (key 0x4e545030)
[2024-12-23 10:30:45] INFO: NTPSec SHM initialized successfully
[2024-12-23 10:30:45] INFO: GPIO initialized successfully
[2024-12-23 10:30:45] INFO: Starting DCF77 decode loop
[2024-12-23 10:31:00] INFO: DCF77 time: winter 2024-12-23 Mon 10:31
```

### Decoding errors

```
[2024-12-23 10:32:00] ERROR: DCF77 decode failed: minute_parity_error 
[2024-12-23 10:32:00] ERROR: Received: winter 2024-12-23 Mon 10:32
```

Common error types:
- `minute_length_error`: Minute was too short or too long
- `bit0_error`: Minute marker bit incorrect
- `bit20_error`: Time/date start marker bit incorrect
- `dst_error`: DST flag bits inconsistent
- `minute_parity_error`, `hour_parity_error`: Parity check failed
- `minute_bcd_error`, `hour_bcd_error`, `date_bcd_error`: Invalid BCD value

## Troubleshooting

### Service won't start

1. Check the service status for detailed error messages:
   ```bash
   sudo systemctl status dcf77pi-ntpsec.service
   journalctl -u dcf77pi-ntpsec.service -n 50
   ```

2. Common issues:
   - **Config file not found**: Ensure `/etc/dcf77pi/config.json` exists
   - **GPIO access denied**: Run as root or fix permissions
   - **SHM creation failed**: Check if another process is using the same SHM unit

### No time decodes (only errors)

1. Check DCF77 signal reception:
   - Ensure the receiver is properly connected to the GPIO pin
   - Check antenna orientation (horizontal for best reception in Europe)
   - Verify `activehigh` setting matches your receiver module
   - Check distance from interference sources

2. Test with the interactive tool first:
   ```bash
   sudo dcf77pi
   ```

### NTPSec not using DCF77

1. **Verify NTPSec is using the correct config file:**
   ```bash
   systemctl status ntpsec | grep "Command line"
   ```
   
   If you see `-c /run/ntpsec/ntp.conf.dhcp`, NTPSec is using a DHCP-generated config instead of `/etc/ntpsec/ntp.conf`. Follow the systemd override steps in the NTPSec Configuration section above.

2. **Check NTPSec logs:**
   ```bash
   journalctl -u ntpsec.service -n 50
   ```

3. **Verify the refclock is configured correctly:**
   ```bash
   cat /etc/ntpsec/ntp.conf | grep -A 2 refclock
   ```

4. **Check that both services are running:**
   ```bash
   sudo systemctl status dcf77pi-ntpsec.service
   sudo systemctl status ntpsec.service
   ```

5. **Ensure shared memory segment exists:**
   ```bash
   ipcs -m | grep 4e545030
   ```

6. **Check if SHM appears in ntpq output:**
   ```bash
   ntpq -p
   ```
   
   If `SHM(0)` appears but has no `*` or `+` indicator, wait 5-10 minutes for NTPSec to collect enough samples. Watch the `reach` value increase from 0 to 377.

### Time offset issues

If NTPSec shows a consistent offset from DCF77:

1. Adjust the `time1` parameter in `/etc/ntpsec/ntp.conf`:
   ```
   refclock shm unit 0 refid DCF77 prefer time1 0.050
   ```
   
2. The `time1` value is in seconds (positive or negative) and compensates for delays in signal processing.

## Uninstalling

**Note:** Running `sudo make uninstall` automatically stops, disables, and removes the systemd service on Linux systems.

If you need to manually uninstall:

1. Stop and disable the service:
   ```bash
   sudo systemctl stop dcf77pi-ntpsec.service
   sudo systemctl disable dcf77pi-ntpsec.service
   ```

2. Remove the service file:
   ```bash
   sudo rm /etc/systemd/system/dcf77pi-ntpsec.service
   sudo systemctl daemon-reload
   ```

3. Remove shared memory segment (optional):
   ```bash
   ipcrm -M 0x4e545030
   ```

4. Uninstall the binaries:
   ```bash
   sudo make uninstall
   ```

## Technical Details

### NTPSec SHM Protocol

The daemon uses NTPSec's shared memory protocol mode 1:
- Increments a counter before and after updating time values
- NTPSec reads the time only if the counter is stable
- Provides both DCF77 timestamp and local receive timestamp
- Reports leap second announcements to NTPSec

### Shared Memory Key

The SHM segment key is calculated as: `0x4e545030 + unit`
- Unit 0: `0x4e545030` (default)
- Unit 1: `0x4e545031`
- Unit 2: `0x4e545032`
- Unit 3: `0x4e545033`

### Precision

DCF77 provides second-level precision. The daemon reports `-10` precision to NTPSec (2^-10 ≈ 1ms) which is realistic given signal processing delays.

## See Also

- Main project: [README.md](README.md)
- Configuration guide: [CONFIG.md](CONFIG.md)
- NTPSec documentation: https://docs.ntpsec.org/
- DCF77 information: https://en.wikipedia.org/wiki/DCF77
