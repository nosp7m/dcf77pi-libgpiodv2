# dcf77pi-libgpiodv2

**This repository is a fork of the archived repository https://github.com/rene0/dcf77pi.<br>Main contribution is the code migration to libgpiodv2, which makes the project usable on Raspberry Pi OS Trixie.**

---

Yet another DCF77 decoder. This one is intended for the Raspberry Pi platform
but might work on other devices using GPIO pins too.

For the Raspberry Pi, connect a stand-alone DCF77 receiver to a GPIO
communication pin (default is 17) and the GPIO GND/3.3V pins (not 5V, this will
break the Raspberry Pi).

An example schematics of a receiver is shown in receiver.fcd which can be shown
using the FidoCadJ package.

The software comes with three binaries and a library:

* dcf77pi : Live decoding from the GPIO pins in interactive mode. Useable keys
  are shown at the bottom of the screen. The backspace key can be used to
  correct the last typed character of the input text (when changing the name of
  the log file).
* dcf77pi-analyze filename : Decode from filename instead of the GPIO pins.
  Output is generated in report mode.
* dcf77pi-readpin [-qr] : Program to test reading from the GPIO pins and decode
  the resulting bit. Send a SIGINT (Ctrl-C) to stop the program. Optional
  parameters are:
  * -q do not show the raw input, default is to show it.
  * -r raw mode, bypass the normal bit reception routine, default is to use it.
* libdcf77.so: The shared library containing common routines for reading bits
  (either from a log file or the GPIO pins) and to decode the date, time and
  third party buffer. Both dcf77pi and dcf77pi-analyze use this library. Header
  files to use the library in your own software are supplied.

The meaning of the keywords in config.json is:

* pin           = GPIO pin number (0-65535)
* iodev         = GPIO device number (FreeBSD only)
* gpiochip      = GPIO chip device path (Linux only, default: /dev/gpiochip0)
* bias          = GPIO line bias (Linux only): "disabled", "pull-up", or "pull-down"
                  (default: "disabled"). Most DCF77 modules have their own pull
                  resistors, so "disabled" is usually correct. Use "pull-up" if
                  your module requires it.
* activehigh    = pulses are active high (true) or passive high (false)
* freq          = sample frequency in Hz (10-155000)
* outlogfile    = name of the output logfile which can be read back using
  dcf77pi-analyze (default empty). The log file itself only stores the
  received bits, but not the decoded date and time.

Depending on your operating system and distribution, you might need to copy
config.json.sample to config.json (in the same directory) to get started. You
might also want to check and update the provided configuration to match your
setup.

**For detailed configuration information and troubleshooting, see [CONFIG.md](CONFIG.md).**

**Note for Linux users:** On Raspberry Pi OS Trixie and newer, GPIO access uses
the modern libgpiod v2 library instead of the deprecated sysfs interface. The
`gpiochip` parameter (optional, defaults to `/dev/gpiochip0`) specifies which
GPIO chip to use.

---

The end of the minute is noted by the absence of high pulses. An absence of low
pulses probably means that the transmitter is out of range. Any other situation
will result in a logical read error.

With permission (comment 5916), the method described at
http://blog.blinkenlight.net/experiments/dcf77/binary-clock is used to receive
the bits.

Currently supporrted platforms:
* FreeBSD, Linux: full support
* Cygwin, MacOS, NetBSD: supported without GPIO communication for live decoding
* Windows: only via Cygwin

You will need to install a json-c development package and a package providing
pkg-config to get the required header files and the .so library files. For
example, on FreeBSD:
```sh
% sudo pkg install json-c pkgconf
```

On Linux, you will also have to install an (n)curses development package and
libgpiod (v2.0 or later) using your package manager. For example, on Raspbian/
Raspberry Pi OS Trixie:

```sh
% sudo apt-get install libncurses5-dev libjson-c-dev libgpiod-dev pkgconf
```

**Note:** For Raspberry Pi OS Bookworm or older, libgpiod v2 may not be
available in the default repositories. Consider upgrading to Trixie or building
libgpiod from source.

**Note:** libgpiod-dev is required for Raspberry Pi OS Trixie and newer. For
older versions, you may need to install libgpiod2 or compile libgpiod v2 from
source.

To build and install the program into /usr/bin , the library into /usr/lib and
the configuration file into /etc/dcf77pi :

```sh
% make clean
% make PREFIX=/usr ETCDIR=/etc/dcf77pi
% sudo make install PREFIX=/usr ETCDIR=/etc/dcf77pi
```

After installation, copy the sample configuration:

```sh
% sudo cp /etc/dcf77pi/config.json.sample /etc/dcf77pi/config.json
% sudo nano /etc/dcf77pi/config.json  # Edit as needed
```

**Note:** The ETCDIR parameter is important! If you install with `PREFIX=/usr`
but don't specify `ETCDIR=/etc/dcf77pi`, the binary will look for the config
in `/usr/etc/dcf77pi` instead of the standard `/etc/dcf77pi` location.

On FreeBSD, dcf77pi and dcf77pi-readpin need to be run as root due to the
permissions of /dev/gpioc\* , but this can be prevented by changing the
permissions of the device node:
```sh
# chmod 0660 /dev/gpioc*
```
And to make the change persistent across reboots:
```sh
# echo "perm gpioc* 0660" >> /etc/devfs.conf
```

On Raspbian Linux, the default permissions allow running dcf77pi and
dcf77pi-readpin as a normal user (typically "pi"), no extra configuration is
needed.

Setting the system time via dcf77pi still requires enhanced privileges (e.g.,
root).
