# dcf77pi-daemon - DCF77 NTPsec/Chrony Daemon

## Overview

`dcf77pi-daemon` is a systemd daemon that decodes DCF77 time signals from a GPIO-connected receiver and provides the time to both NTPsec and Chrony via shared memory (SHM). This allows your Raspberry Pi to act as a stratum 1 NTP server using DCF77 as the reference clock.

## Features

- **Daemon mode**: Runs as a systemd service in the background
- **Dual SHM integration**: Provides time via both NTPsec's and Chrony's SHM interfaces
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
make dcf77pi-daemon
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

**New parameters:**
- `shm_unit`: NTPsec/Chrony SHM unit number (0-3, default 0). This corresponds to the System V SHM segment that NTPsec/Chrony will read from. Use different numbers if you have multiple time sources.

**Note:** The `outlogfile` parameter is ignored by `dcf77pi-daemon`. All logging goes to systemd journal only.

### 2. NTPsec Configuration

#### Step 1: Edit the NTPsec configuration file

Add the DCF77 SHM refclock to your NTPsec configuration (`/etc/NTPsec/ntp.conf`):

```
# DCF77 via shared memory (SHM unit 0)
refclock shm unit 0 refid DCF77 prefer
# Adjust time1 offset if needed (in seconds)
# time1 0.0

# Optional: Set stratum if this is your primary time source
# tos minsane 1
```

If you changed `shm_unit` in config.json, make sure the `unit` number matches.

#### Step 2: Configure NTPsec to use the static configuration

Some systems (especially when using DHCP) configure NTPsec to use a dynamically generated config file (e.g., `/run/NTPsec/ntp.conf.dhcp`) instead of the static `/etc/NTPsec/ntp.conf`. To ensure NTPsec uses your static configuration:

**Check which config file is being used:**

```bash
systemctl status NTPsec | grep "Command line"
```

If you see `-c /run/NTPsec/ntp.conf.dhcp` or similar, you need to override this.

**Create a systemd service override:**

```bash
sudo systemctl edit NTPsec
```

Add these lines in the editor that opens:

```ini
[Service]
ExecStart=
ExecStart=/usr/sbin/ntpd -p /run/ntpd.pid -g -u NTPsec:NTPsec
```

The first `ExecStart=` clears the original command, and the second sets it to use the default `/etc/NTPsec/ntp.conf` (without the `-c` parameter).

Save and exit the editor (Ctrl+O, Enter, Ctrl+X in nano).

**Alternative manual method:**

```bash
# Create override directory
sudo mkdir -p /etc/systemd/system/NTPsec.service.d/

# Create override file
sudo nano /etc/systemd/system/NTPsec.service.d/override.conf
```

Add this content:

```ini
[Service]
ExecStart=
ExecStart=/usr/sbin/ntpd -p /run/ntpd.pid -g -u NTPsec:NTPsec
```

#### Step 3: Apply changes and restart NTPsec

```bash
# Reload systemd configuration
sudo systemctl daemon-reload

# Restart NTPsec
sudo systemctl restart NTPsec

# Verify it's using the correct config file
systemctl status NTPsec | grep "Command line"
```

You should now see the command line **without** the `-c` parameter, meaning it's using `/etc/NTPsec/ntp.conf`.

### 3. Chrony Configuration (Alternative to NTPsec)

If you prefer to use Chrony instead of NTPsec (or want to use both), configure Chrony to read from the shared memory segment.

**Important:** DCF77 provides minute-precision time updates (one sample per minute). The Chrony configuration must be optimized for this low update rate.

#### Step 1: Edit the Chrony configuration file

Edit `/etc/chrony/chrony.conf` (or `/etc/chrony.conf` on some systems) and add:

```
# DCF77 via SHM shared memory (unit 0)
# DCF77 provides 1-minute updates with ~100ms precision
refclock SHM 0 refid DCF precision 1e-1 poll 6 delay 0.5 filter 3 prefer
```

**Configuration parameters explained:**

- `SHM 0`: Use shared memory unit 0 (must match `shm_unit` in config.json)
- `refid DCF`: Reference ID displayed in sources (use "DCF" or "DCF77")
- `precision 1e-1`: DCF77 radio signal has ~100ms (0.1 second) precision
  - This is realistic for longwave radio time signals
  - Don't use values smaller than 1e-2 (10ms) for DCF77
- `poll 6`: Poll interval = 2^6 = 64 seconds
  - Good match for DCF77's 1-minute update rate
  - Default poll intervals (8-16 seconds) are too fast for DCF77
  - Valid range: 4-10 (16 seconds to 17 minutes)
- `delay 0.5`: Compensate for signal propagation and decoding delay (~0.5 seconds)
  - Adjust based on distance from Mainflingen transmitter (Germany)
  - Typical values: 0.2-0.8 seconds
  - Fine-tune using `chronyc sourcestats` after collecting data
- `filter 3`: Apply median filter over 3 samples
  - Helps reject outliers from radio interference
  - Recommended: 3-5 samples
- `prefer`: Mark as preferred time source (optional)
  - Use if DCF77 is your primary/only refclock
  - Omit if you have other reference clocks

**Alternative configurations:**

For initial testing (monitor without using for sync):
```
refclock SHM 0 refid DCF precision 1e-1 poll 6 noselect
```

For areas with good DCF77 reception and no other refclocks:
```
refclock SHM 0 refid DCF precision 1e-1 poll 6 delay 0.5 filter 5 prefer trust
```

If you changed `shm_unit` in config.json, ensure the SHM unit number matches.

#### Step 2: Configure additional Chrony settings (recommended)

Add these settings to `/etc/chrony/chrony.conf` for better DCF77 operation:

```
# Allow larger initial corrections (helpful at system startup)
makestep 1.0 3

# Increase maxdistance for low-rate refclocks like DCF77
maxdistance 16.0

# Enable logging for monitoring (optional)
logdir /var/log/chrony
log measurements statistics tracking refclocks
```

#### Step 3: Restart Chrony

```bash
sudo systemctl restart chronyd
```

#### Step 4: Monitor Chrony synchronization

**Check sources:**
```bash
chronyc sources -v
```

Expected output showing DCF77 as a source:
```
MS Name/IP address         Stratum Poll Reach LastRx Last sample               
===============================================================================
#* DCF                          0   6   377    32    -45ms[  -47ms] +/-  100ms
```

**Understanding the output:**
- `#*` - Currently selected and synchronized
- `#?` - Unreachable or no valid samples
- `#-` - Not combined (e.g., marked `noselect`)
- `Stratum 0` - Reference clock (highest quality)
- `Poll 6` - Polling every 64 seconds
- `Reach 377` - Full reachability (octal 377 = binary 11111111 = 8 successful polls)
- `LastRx` - Seconds since last sample received
- Offset should be stable within ±100ms for good reception

**Check detailed statistics:**
```bash
chronyc sourcestats -v
```

Expected output:
```
Name/IP Address            NP  NR  Span  Frequency  Freq Skew  Offset  Std Dev
===============================================================================
DCF                         8   4   448      +0.01      15.2    -45ms    23ms
```

**Check refclock-specific data:**
```bash
chronyc ntpdata DCF
```

**Monitor real-time:**
```bash
# Watch sources update every 2 seconds
watch -n 2 chronyc sources

# View system journal for daemon logs
journalctl -u dcf77pi-daemon -f
```

#### Troubleshooting Chrony

**Problem: "Can't synchronise: no selectable sources"**
- Check that DCF77 daemon is running: `systemctl status dcf77pi-daemon`
- Verify SHM segment exists: `ipcs -m | grep 4e54`
- Ensure `Reach` value increases in `chronyc sources`
- Remove `noselect` option if present

**Problem: Large frequency errors (>1000 ppm)**
- Verify daemon is providing new samples every minute (check journal logs)
- Ensure `poll` interval is appropriate (6 or higher for DCF77)
- Check system clock isn't being adjusted by other sources

**Problem: Poor offset stability (jumping around)**
- Increase `filter` value (5-7 samples)
- Check DCF77 signal quality (may need better antenna/location)
- Adjust `delay` parameter based on your location
- Verify no RF interference near receiver

### 4. Permissions

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
sudo cp dcf77pi-daemon.service /etc/systemd/system/
```

2. Reload systemd:

```bash
sudo systemctl daemon-reload
```

3. Enable the service to start at boot:

```bash
sudo systemctl enable dcf77pi-daemon.service
```

4. Start the service:

```bash
sudo systemctl start dcf77pi-daemon.service
```

## Monitoring

### View service status

```bash
sudo systemctl status dcf77pi-daemon.service
```

### View logs

```bash
# View all logs
journalctl -u dcf77pi-daemon.service

# Follow logs in real-time
journalctl -u dcf77pi-daemon.service -f

# View only today's logs
journalctl -u dcf77pi-daemon.service --since today

# View last 100 lines
journalctl -u dcf77pi-daemon.service -n 100
```

### Check NTPsec synchronization

```bash
# Check NTPsec peer status
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
1. `reach` to increase from 0 to 377 as NTPsec collects samples
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
[2024-12-23 10:30:45] INFO: dcf77pi-daemon daemon starting
[2024-12-23 10:30:45] INFO: Initializing SHM unit 0 (key 0x4e545030)
[2024-12-23 10:30:45] INFO: SHM initialized successfully
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
   sudo systemctl status dcf77pi-daemon.service
   journalctl -u dcf77pi-daemon.service -n 50
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

### NTPsec not using DCF77

1. **Verify NTPsec is using the correct config file:**
   ```bash
   systemctl status NTPsec | grep "Command line"
   ```
   
   If you see `-c /run/NTPsec/ntp.conf.dhcp`, NTPsec is using a DHCP-generated config instead of `/etc/NTPsec/ntp.conf`. Follow the systemd override steps in the NTPsec Configuration section above.

2. **Check NTPsec logs:**
   ```bash
   journalctl -u NTPsec.service -n 50
   ```

3. **Verify the refclock is configured correctly:**
   ```bash
   cat /etc/NTPsec/ntp.conf | grep -A 2 refclock
   ```

4. **Check that both services are running:**
   ```bash
   sudo systemctl status dcf77pi-daemon.service
   sudo systemctl status NTPsec.service
   ```

5. **Ensure shared memory segment exists:**
   ```bash
   ipcs -m | grep 4e545030
   ```

6. **Check if SHM appears in ntpq output:**
   ```bash
   ntpq -p
   ```
   
   If `SHM(0)` appears but has no `*` or `+` indicator, wait 5-10 minutes for NTPsec to collect enough samples. Watch the `reach` value increase from 0 to 377.

### Time offset issues

If NTPsec shows a consistent offset from DCF77:

1. Adjust the `time1` parameter in `/etc/NTPsec/ntp.conf`:
   ```
   refclock shm unit 0 refid DCF77 prefer time1 0.050
   ```
   
2. The `time1` value is in seconds (positive or negative) and compensates for delays in signal processing.

## Uninstalling

**Note:** Running `sudo make uninstall` automatically stops, disables, and removes the systemd service on Linux systems.

If you need to manually uninstall:

1. Stop and disable the service:
   ```bash
   sudo systemctl stop dcf77pi-daemon.service
   sudo systemctl disable dcf77pi-daemon.service
   ```

2. Remove the service file:
   ```bash
   sudo rm /etc/systemd/system/dcf77pi-daemon.service
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

### NTPsec SHM Protocol

The daemon uses NTPsec's shared memory protocol mode 1:
- Increments a counter before and after updating time values
- NTPsec reads the time only if the counter is stable
- Provides both DCF77 timestamp and local receive timestamp
- Reports leap second announcements to NTPsec

### Shared Memory Key

The SHM segment key is calculated as: `0x4e545030 + unit`
- Unit 0: `0x4e545030` (default)
- Unit 1: `0x4e545031`
- Unit 2: `0x4e545032`
- Unit 3: `0x4e545033`

### Precision

DCF77 provides second-level precision. The daemon reports `-10` precision to NTPsec (2^-10 ≈ 1ms) which is realistic given signal processing delays.

## See Also

- Main project: [README.md](README.md)
- Configuration guide: [CONFIG.md](CONFIG.md)
- NTPsec documentation: https://docs.NTPsec.org/
- DCF77 information: https://en.wikipedia.org/wiki/DCF77
