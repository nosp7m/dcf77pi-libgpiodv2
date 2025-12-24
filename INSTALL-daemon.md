# Quick Installation Guide for dcf77pi-daemon

This guide will help you quickly set up the DCF77 NTPSec daemon on your Raspberry Pi.

## Prerequisites

- Raspberry Pi with Raspberry Pi OS (Trixie or newer recommended)
- DCF77 receiver module connected to GPIO
- Root/sudo access
- NTPSec installed (`sudo apt install ntpsec` if not already installed)

## Step 1: Build and Install

```bash
# Install dependencies
sudo apt install build-essential pkg-config libjson-c-dev libgpiod-dev

# Clone and build (if not already done)
cd /path/to/dcf77pi
make
sudo make install
```

## Step 2: Configure DCF77

```bash
# Copy sample config if needed
sudo mkdir -p /etc/dcf77pi
sudo cp /usr/local/etc/dcf77pi/config.json.sample /etc/dcf77pi/config.json

# Edit configuration
sudo nano /etc/dcf77pi/config.json
```

Minimal configuration example:
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

**Important settings:**
- `pin`: GPIO pin number where DCF77 receiver is connected (default: 15)
- `activehigh`: Set to `true` if your receiver outputs HIGH for signal, `false` for LOW
- `shm_unit`: Use 0 for first time source, 1, 2, 3 for additional sources

## Step 3: Test the Daemon Manually

Before setting up as a service, test that it works:

```bash
sudo /usr/local/bin/dcf77pi-daemon
```

You should see output like:
```
[2024-12-23 10:30:45] INFO: dcf77pi-daemon daemon starting
[2024-12-23 10:30:45] INFO: Initializing NTPSec SHM unit 0 (key 0x4e545030)
[2024-12-23 10:30:45] INFO: GPIO initialized successfully
[2024-12-23 10:30:45] INFO: Starting DCF77 decode loop
```

Wait for a successful decode (up to 2 minutes):
```
[2024-12-23 10:31:00] INFO: DCF77 time: winter 2024-12-23 Mon 10:31
```

Press Ctrl+C to stop if successful.

**If you get errors:**
- GPIO access denied → Run with `sudo` or fix permissions
- Config file not found → Check path `/etc/dcf77pi/config.json`
- No signal/decode errors → Check receiver connection and antenna placement

## Step 4: Install Systemd Service

The `make install` step automatically installs and enables the systemd service on Linux systems.

If you need to manually install the service:

```bash
# Copy service file
sudo cp dcf77pi-daemon.service /etc/systemd/system/

# Reload systemd
sudo systemctl daemon-reload

# Enable service to start at boot
sudo systemctl enable dcf77pi-daemon.service

# Start the service
sudo systemctl start dcf77pi-daemon.service

# Check status
sudo systemctl status dcf77pi-daemon.service
```

## Step 5: Configure NTPSec

```bash
# Edit NTPSec configuration
sudo nano /etc/ntpsec/ntp.conf
```

Add the DCF77 refclock:
```
# DCF77 via shared memory
refclock shm unit 0 refid DCF77 prefer
```

**Note:** The `unit 0` must match `shm_unit` in your DCF77 config.json.

Restart NTPSec:
```bash
sudo systemctl restart ntpsec
```

## Step 6: Verify It's Working

Wait a few minutes for synchronization, then check:

```bash
# Check NTPSec peers
ntpq -p
```

Expected output:
```
     remote           refid      st t when poll reach   delay   offset  jitter
==============================================================================
*SHM(0)          .DCF77.         0 l   11   16  377    0.000    0.123   0.045
```

The `*` means DCF77 is the selected time source!

Check daemon logs:
```bash
journalctl -u dcf77pi-daemon.service -f
```

Check shared memory:
```bash
ipcs -m | grep 4e54503
```

## Troubleshooting

### Service fails to start

```bash
# Check detailed logs
journalctl -u dcf77pi-daemon.service -n 50
```

### No time decodes (continuous errors)

1. Check antenna orientation (horizontal, perpendicular to Mainflingen, Germany)
2. Move away from electronic interference
3. Verify GPIO connection
4. Test with interactive mode: `sudo dcf77pi`

### NTPSec not synchronizing

```bash
# Check NTPSec status
journalctl -u ntpsec.service -n 50

# Verify SHM segment exists
ipcs -m | grep 4e545030

# Check both services are running
systemctl status dcf77pi-daemon.service
systemctl status ntpsec.service
```

## Advanced Options

### Running as non-root user

```bash
# Create user
sudo useradd -r -s /bin/false dcf77pi

# Grant GPIO access (method depends on your system)
sudo usermod -a -G gpio dcf77pi

# Edit service file
sudo nano /etc/systemd/system/dcf77pi-daemon.service
```

Uncomment these lines:
```
User=dcf77pi
Group=dcf77pi
```

Reload and restart:
```bash
sudo systemctl daemon-reload
sudo systemctl restart dcf77pi-daemon.service
```

### Multiple DCF77 receivers

Use different GPIO pins and SHM units:

Config file 1 (`/etc/dcf77pi/config.json`):
```json
{
    "pin": 17,
    "shm_unit": 0
}
```

Config file 2 (`/etc/dcf77pi/config2.json`):
```json
{
    "pin": 27,
    "shm_unit": 1
}
```

You'll need to create a second service file that uses `config2.json`.

## Monitoring

Daily health check:
```bash
# View today's successful decodes
journalctl -u dcf77pi-daemon.service --since today | grep "DCF77 time:"

# Count decode errors today
journalctl -u dcf77pi-daemon.service --since today | grep "ERROR" | wc -l
```

## Next Steps

- See [README-daemon.md](README-daemon.md) for complete documentation
- Configure NTP clients to use your Raspberry Pi as time server
- Set up monitoring/alerting for the service
- Consider adding other time sources (GPS, network NTP) as backup

## Getting Help

If you encounter issues:

1. Check the comprehensive documentation in [README-daemon.md](README-daemon.md)
2. Review system logs: `journalctl -u dcf77pi-daemon.service -n 100`
3. Test with interactive tool: `sudo dcf77pi`
4. Check project issues on GitHub

Happy timekeeping! 🕐
