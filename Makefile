# Copyright 2013-2020 René Ladan
# SPDX-License-Identifier: BSD-2-Clause

.PHONY: all clean install install-strip doxygen install-doxygen uninstall \
	uninstall-doxygen cppcheck iwyu

# Default to standard system paths
PREFIX?=/usr
ETCDIR?=/etc/dcf77pi

CFLAGS+=-Wall -D_POSIX_C_SOURCE=200809L -DETCDIR=\"$(ETCDIR)\" \
	-g -std=c99
INSTALL?=install
INSTALL_PROGRAM?=$(INSTALL)
CPPCHECK_ARGS?=--enable=all --inconclusive --language=c --std=c99 \
	-DETCDIR=\"$(ETCDIR)\"
# $(shell ...) does not work with FreeBSD make
JSON_C?=`pkg-config --cflags json-c`
JSON_L?=`pkg-config --libs json-c`
GPIOD_C?=`pkg-config --cflags libgpiod 2>/dev/null || echo ""`
GPIOD_L?=`pkg-config --libs libgpiod 2>/dev/null || echo ""`

all: libdcf77.so dcf77pi dcf77pi-analyze dcf77pi-readpin dcf77pi-daemon kevent-demo

hdrlib=input.h decode_time.h decode_alarm.h setclock.h mainloop.h \
	bits1to14.h calendar.h
srclib=${hdrlib:.h=.c}
objlib=${hdrlib:.h=.o}
objbin=dcf77pi.o dcf77pi-analyze.o dcf77pi-readpin.o dcf77pi-daemon.o kevent-demo.o

input.o: input.c input.h
	$(CC) -fpic $(CFLAGS) $(JSON_C) $(GPIOD_C) -c input.c -o $@
decode_time.o: decode_time.c decode_time.h calendar.h
	$(CC) -fpic $(CFLAGS) -c decode_time.c -o $@
decode_alarm.o: decode_alarm.c decode_alarm.h
	$(CC) -fpic $(CFLAGS) -c decode_alarm.c -o $@
setclock.o: setclock.c setclock.h decode_time.h input.h calendar.h
	$(CC) -fpic $(CFLAGS) -c setclock.c -o $@
mainloop.o: mainloop.c mainloop.h input.h bits1to14.h decode_alarm.h \
	decode_time.h setclock.h
	$(CC) -fpic $(CFLAGS) -c mainloop.c -o $@
bits1to14.o: bits1to14.c bits1to14.h input.h
	$(CC) -fpic $(CFLAGS) -c bits1to14.c -o $@
calendar.o: calendar.c calendar.h
	$(CC) -fpic $(CFLAGS) -c calendar.c -o $@

libdcf77.so: $(objlib)
	$(CC) -shared -o $@ $(objlib) -lm -lpthread $(JSON_L) $(GPIOD_L)

dcf77pi.o: bits1to14.h decode_alarm.h decode_time.h input.h \
	mainloop.h calendar.h dcf77pi.c
	$(CC) -fpic $(CFLAGS) $(JSON_C) -c dcf77pi.c -o $@
dcf77pi: dcf77pi.o libdcf77.so
	$(CC) -o $@ dcf77pi.o -lncurses libdcf77.so -lpthread $(JSON_L)

dcf77pi-analyze.o: bits1to14.h decode_alarm.h decode_time.h input.h \
	mainloop.h calendar.h dcf77pi-analyze.c
dcf77pi-analyze: dcf77pi-analyze.o libdcf77.so
	$(CC) -fpic $(CFLAGS) -c dcf77pi-analyze.c -o $@
	$(CC) -o $@ dcf77pi-analyze.o libdcf77.so

dcf77pi-readpin.o: input.h dcf77pi-readpin.c
	$(CC) -fpic $(CFLAGS) $(JSON_C) -c dcf77pi-readpin.c -o $@
dcf77pi-readpin: dcf77pi-readpin.o libdcf77.so
	$(CC) -o $@ dcf77pi-readpin.o libdcf77.so $(JSON_L)

dcf77pi-daemon.o: bits1to14.h decode_alarm.h decode_time.h input.h \
	mainloop.h calendar.h dcf77pi-daemon.c
	$(CC) -fpic $(CFLAGS) $(JSON_C) -c dcf77pi-daemon.c -o $@
dcf77pi-daemon: dcf77pi-daemon.o libdcf77.so
	$(CC) -o $@ dcf77pi-daemon.o libdcf77.so $(JSON_L) -lrt

kevent-demo.o: input.h kevent-demo.c
	# __BSD_VISIBLE for FreeBSD < 12.0
	[ `uname -s` = "FreeBSD" ] && $(CC) -fpic $(CFLAGS) $(JSON_C) -c kevent-demo.c -o $@ -D__BSD_VISIBLE=1 || true
kevent-demo: kevent-demo.o libdcf77.so
	[ `uname -s` = "FreeBSD" ] && $(CC) -o $@ kevent-demo.o libdcf77.so $(JSON_L) || true

doxygen:
	doxygen

clean:
	rm -f dcf77pi
	rm -f dcf77pi-analyze
	rm -f dcf77pi-readpin
	rm -f dcf77pi-daemon
	rm -f kevent-demo
	rm -f $(objbin)
	rm -f libdcf77.so $(objlib)

install: libdcf77.so dcf77pi dcf77pi-analyze dcf77pi-readpin dcf77pi-daemon kevent-demo
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	$(INSTALL_PROGRAM) libdcf77.so $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	$(INSTALL_PROGRAM) dcf77pi dcf77pi-analyze dcf77pi-readpin dcf77pi-daemon \
		$(DESTDIR)$(PREFIX)/bin
	[ `uname -s` = "FreeBSD" ] && $(INSTALL_PROGRAM) kevent-demo \
		$(DESTDIR)$(PREFIX)/bin || true
	mkdir -p $(DESTDIR)$(PREFIX)/include/dcf77pi
	$(INSTALL) -m 0644 $(hdrlib) $(DESTDIR)$(PREFIX)/include/dcf77pi
	mkdir -p $(DESTDIR)$(ETCDIR)
	$(INSTALL) -m 0644 etc/dcf77pi/config.json \
		$(DESTDIR)$(ETCDIR)/config.json.sample
	# Copy config.json only if it doesn't exist
	if [ ! -f $(DESTDIR)$(ETCDIR)/config.json ]; then \
		$(INSTALL) -m 0644 etc/dcf77pi/config.json $(DESTDIR)$(ETCDIR)/config.json && \
		echo "Installed default config.json to $(ETCDIR)/config.json"; \
	else \
		echo "Config file $(ETCDIR)/config.json already exists, not overwriting"; \
	fi
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/dcf77pi
	$(INSTALL) -m 0644 LICENSE.md \
		$(DESTDIR)$(PREFIX)/share/doc/dcf77pi
	# Install systemd service file (Linux only)
	if [ `uname -s` = "Linux" ] && [ -d /etc/systemd/system ]; then \
		$(INSTALL) -m 0644 dcf77pi-daemon.service /etc/systemd/system/ && \
		systemctl daemon-reload && \
		systemctl enable dcf77pi-daemon.service && \
		echo "Systemd service installed and enabled. Start with: systemctl start dcf77pi-daemon.service"; \
	fi

install-strip:
	$(MAKE) INSTALL_PROGRAM='$(INSTALL_PROGRAM) -s' install

install-doxygen: html
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/dcf77pi
	cp -R html $(DESTDIR)$(PREFIX)/share/doc/dcf77pi

install-md:
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/dcf77pi
	$(INSTALL) -m 0644 *.md \
		$(DESTDIR)$(PREFIX)/share/doc/dcf77pi

uninstall: uninstall-doxygen uninstall-md
	# Stop and disable systemd service (Linux only)
	if [ `uname -s` = "Linux" ] && [ -f /etc/systemd/system/dcf77pi-daemon.service ]; then \
		systemctl stop dcf77pi-daemon.service || true; \
		systemctl disable dcf77pi-daemon.service || true; \
		rm -f /etc/systemd/system/dcf77pi-daemon.service; \
		systemctl daemon-reload; \
		echo "Systemd service stopped, disabled, and removed"; \
	fi
	rm -f $(DESTDIR)$(PREFIX)/lib/libdcf77.so
	rm -f $(DESTDIR)$(PREFIX)/bin/dcf77pi
	rm -f $(DESTDIR)$(PREFIX)/bin/dcf77pi-analyze
	rm -f $(DESTDIR)$(PREFIX)/bin/dcf77pi-readpin
	rm -f $(DESTDIR)$(PREFIX)/bin/dcf77pi-daemon
	rm -f $(DESTDIR)$(PREFIX)/bin/kevent-demo
	rm -rf $(DESTDIR)$(PREFIX)/include/dcf77pi
	rm -f $(DESTDIR)$(ETCDIR)/config.json.sample
	rm -f $(DESTDIR)$(ETCDIR)/config.json
	rmdir $(DESTDIR)$(ETCDIR) 2>/dev/null || true
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/dcf77pi

uninstall-doxygen:
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/dcf77pi/html

uninstall-md:
	rm -f $(DESTDIR)$(PREFIX)/share/doc/dcf77pi/*.md

cppcheck:
	cppcheck -D__CYGWIN__ $(CPPCHECK_ARGS) . || true
	cppcheck -D__linux__ $(CPPCHECK_ARGS) . || true
	cppcheck -D__FreeBSD__ -D__FreeBSD_version=900022 \
		$(CPPCHECK_ARGS) . || true

iwyu:
	$(MAKE) clean
	$(MAKE) -k CC=include-what-you-use $(objlib) $(objbin) || true
