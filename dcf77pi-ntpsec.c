// Copyright 2024 René Ladan and contributors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * dcf77pi-ntpsec: DCF77 decoder daemon with NTPSec shared memory integration
 * 
 * This daemon runs as a systemd service, decodes DCF77 time signals from GPIO,
 * and provides the time to NTPSec via its shared memory (SHM) interface.
 * All output goes to systemd journal (stdout/stderr).
 */

#include "bits1to14.h"
#include "calendar.h"
#include "decode_alarm.h"
#include "decode_time.h"
#include "input.h"
#include "mainloop.h"
#include "setclock.h"

#include "json_object.h"
#include "json_util.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* NTPSec shared memory structure (from ntpd source) */
struct shmTime {
	int    mode; /* 0 - if valid set
	              *   use values, 
	              *   clear valid
	              * 1 - if valid set 
	              *   if count before and after read of values is equal,
	              *     use values 
	              *   clear valid
	              */
	volatile int count;
	time_t clockTimeStampSec;
	int    clockTimeStampUSec;
	time_t receiveTimeStampSec;
	int    receiveTimeStampUSec;
	int    leap;
	int    precision;
	int    nsamples;
	volatile int valid;
	unsigned clockTimeStampNSec;     /* Unsigned ns timestamps */
	unsigned receiveTimeStampNSec;
	int dummy[8];
};

/* NTPSec SHM unit number (configurable via config.json) */
static int shm_unit = 0;

/* Shared memory segment */
static struct shmTime *shm = NULL;
static int shmid = -1;

/* Signal handler flag */
static volatile sig_atomic_t quit_flag = 0;

/* Logging helper - logs to stdout (systemd journal) */
static void
log_info(const char *fmt, ...)
{
	va_list ap;
	time_t now;
	char timebuf[32];
	struct tm *tm;

	now = time(NULL);
	tm = localtime(&now);
	if (tm != NULL) {
		strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
		printf("[%s] INFO: ", timebuf);
	} else {
		printf("INFO: ");
	}
	
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

static void
log_error(const char *fmt, ...)
{
	va_list ap;
	time_t now;
	char timebuf[32];
	struct tm *tm;

	now = time(NULL);
	tm = localtime(&now);
	if (tm != NULL) {
		strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
		fprintf(stderr, "[%s] ERROR: ", timebuf);
	} else {
		fprintf(stderr, "ERROR: ");
	}
	
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	fflush(stderr);
}

/* Initialize NTPSec shared memory segment */
static int
init_shm(void)
{
	key_t key;
	
	/* NTPSec uses key = 0x4e545030 + unit (NTP0, NTP1, etc.) */
	key = 0x4e545030 + shm_unit;
	
	log_info("Initializing NTPSec SHM unit %d (key 0x%08x)", shm_unit, key);
	
	/* Create or attach to shared memory segment */
	shmid = shmget(key, sizeof(struct shmTime), IPC_CREAT | 0600);
	if (shmid == -1) {
		log_error("Failed to create/attach SHM segment: %s", strerror(errno));
		return errno;
	}
	
	/* Attach to shared memory */
	shm = (struct shmTime *)shmat(shmid, NULL, 0);
	if (shm == (struct shmTime *)(-1)) {
		log_error("Failed to attach to SHM segment: %s", strerror(errno));
		shm = NULL;
		return errno;
	}
	
	/* Initialize the shared memory structure */
	memset(shm, 0, sizeof(struct shmTime));
	shm->mode = 1;  /* Mode 1: use count for synchronization */
	shm->precision = -10;  /* ~1ms precision (2^-10 seconds) */
	shm->nsamples = 3;
	shm->valid = 0;
	
	log_info("NTPSec SHM initialized successfully");
	return 0;
}

/* Update NTPSec shared memory with current time */
static void
update_shm(struct tm dcf_time, int leap_second)
{
	struct timeval tv;
	time_t dcf_unix_time;
	struct tm std_time;
	
	if (shm == NULL) {
		return;
	}
	
	/* Convert DCF77 time structure to standard tm structure
	 * DCF77 uses: tm_year = actual year (e.g., 2025), tm_mon = 1-12
	 * Standard tm uses: tm_year = years since 1900, tm_mon = 0-11
	 */
	memset(&std_time, 0, sizeof(std_time));
	std_time.tm_sec = 0;  /* DCF77 provides minute precision */
	std_time.tm_min = dcf_time.tm_min;
	std_time.tm_hour = dcf_time.tm_hour;
	std_time.tm_mday = dcf_time.tm_mday;
	std_time.tm_mon = dcf_time.tm_mon - 1;  /* Convert 1-12 to 0-11 */
	std_time.tm_year = dcf_time.tm_year - 1900;  /* Convert to years since 1900 */
	std_time.tm_wday = dcf_time.tm_wday;
	std_time.tm_isdst = dcf_time.tm_isdst;
	
	/* Convert to Unix timestamp */
	dcf_unix_time = mktime(&std_time);
	if (dcf_unix_time == -1) {
		log_error("mktime() failed for DCF77 time");
		return;
	}
	
	/* Get current system time for receive timestamp */
	if (gettimeofday(&tv, NULL) != 0) {
		log_error("gettimeofday() failed: %s", strerror(errno));
		return;
	}
	
	/* Update shared memory using mode 1 protocol */
	shm->valid = 0;  /* Invalidate while updating */
	shm->count++;
	
	shm->clockTimeStampSec = dcf_unix_time;
	shm->clockTimeStampUSec = 0;  /* DCF77 provides second precision */
	shm->clockTimeStampNSec = 0;
	
	shm->receiveTimeStampSec = tv.tv_sec;
	shm->receiveTimeStampUSec = tv.tv_usec;
	shm->receiveTimeStampNSec = tv.tv_usec * 1000;
	
	/* Set leap second indicator */
	if (leap_second) {
		shm->leap = 1;  /* Leap second announced */
	} else {
		shm->leap = 0;  /* Normal */
	}
	
	shm->count++;
	shm->valid = 1;  /* Mark as valid */
}

/* Cleanup NTPSec shared memory */
static void
cleanup_shm(void)
{
	if (shm != NULL) {
		/* Mark as invalid before detaching */
		shm->valid = 0;
		
		if (shmdt(shm) == -1) {
			log_error("Failed to detach SHM segment: %s", strerror(errno));
		}
		shm = NULL;
	}
	
	/* Note: We don't remove the SHM segment (shmctl IPC_RMID) because
	 * NTPSec might still be using it. Let the system admin clean it up
	 * manually if needed with: ipcrm -M 0x4e545030 (or appropriate key)
	 */
	
	log_info("NTPSec SHM cleanup complete");
}

/* Signal handler for graceful shutdown */
static void
signal_handler(int sig)
{
	(void)sig;
	quit_flag = 1;
}

/* Callback: display nothing (we log instead) */
static void
display_bit(struct GB_result bit, int bitpos)
{
	(void)bit;
	(void)bitpos;
	/* Silent - only log on errors or successful decode */
}

/* Callback: display nothing for long minute */
static void
display_long_minute(void)
{
	log_error("Minute too long detected");
}

/* Callback: display minute information */
static void
display_minute(int minlen)
{
	(void)minlen;
	/* Silent unless there's an issue */
}

/* Callback: display alarm (should not happen in normal operation) */
static void
display_alarm(struct alm alarm)
{
	log_info("German civil warning received (decoding error?): %s", 
	    get_region_name(alarm));
}

/* Callback: display unknown third party data */
static void
display_unknown(void)
{
	log_info("Unknown third party data received");
}

/* Callback: display weather data */
static void
display_weather(void)
{
	log_info("Meteotime weather data received");
}

/* Callback: display third party buffer */
static void
display_thirdparty_buffer(const unsigned tpbuf[])
{
	(void)tpbuf;
	/* Silent */
}

/* Callback: display and log decoded time */
static void
display_time(struct DT_result dt, struct tm time)
{
	bool has_error = false;
	char error_msg[512] = "";
	
	/* Check for errors and build error message */
	if (dt.minute_length != emin_ok) {
		strcat(error_msg, "minute_length_error ");
		has_error = true;
	}
	if (!dt.bit0_ok) {
		strcat(error_msg, "bit0_error ");
		has_error = true;
	}
	if (!dt.bit20_ok) {
		strcat(error_msg, "bit20_error ");
		has_error = true;
	}
	if (dt.dst_status == eDST_error) {
		strcat(error_msg, "dst_error ");
		has_error = true;
	}
	if (dt.minute_status == eval_parity) {
		strcat(error_msg, "minute_parity_error ");
		has_error = true;
	} else if (dt.minute_status == eval_bcd) {
		strcat(error_msg, "minute_bcd_error ");
		has_error = true;
	}
	if (dt.hour_status == eval_parity) {
		strcat(error_msg, "hour_parity_error ");
		has_error = true;
	} else if (dt.hour_status == eval_bcd) {
		strcat(error_msg, "hour_bcd_error ");
		has_error = true;
	}
	if (dt.mday_status == eval_parity || dt.wday_status == eval_parity ||
	    dt.month_status == eval_parity || dt.year_status == eval_parity) {
		strcat(error_msg, "date_parity_error ");
		has_error = true;
	}
	if (dt.mday_status == eval_bcd || dt.wday_status == eval_bcd ||
	    dt.month_status == eval_bcd || dt.year_status == eval_bcd) {
		strcat(error_msg, "date_bcd_error ");
		has_error = true;
	}
	
	if (has_error) {
		log_error("DCF77 decode failed: %s", error_msg);
		log_error("Received: %s %04d-%02d-%02d %s %02d:%02d",
		    time.tm_isdst == 1 ? "summer" : time.tm_isdst == 0 ? "winter" : "?",
		    time.tm_year, time.tm_mon, time.tm_mday, 
		    weekday[time.tm_wday], time.tm_hour, time.tm_min);
	} else {
		/* Successful decode - log and update SHM */
		log_info("DCF77 time: %s %04d-%02d-%02d %s %02d:%02d%s%s",
		    time.tm_isdst == 1 ? "summer" : time.tm_isdst == 0 ? "winter" : "UTC",
		    time.tm_year, time.tm_mon, time.tm_mday,
		    weekday[time.tm_wday], time.tm_hour, time.tm_min,
		    dt.dst_announce ? " [DST change announced]" : "",
		    dt.leap_announce ? " [leap second announced]" : "");
		
		/* Update NTPSec shared memory */
		update_shm(time, dt.leap_announce);
	}
}

/* Callback: process setclock result (we don't set clock in daemon mode) */
static struct ML_result
process_setclock_result(struct ML_result in_ml, int bitpos)
{
	(void)bitpos;
	/* We don't set the system clock - NTPSec does that */
	return in_ml;
}

/* Callback: process input (check for quit signal) */
static struct ML_result
process_input(struct ML_result in_ml, int bitpos)
{
	(void)bitpos;
	
	if (quit_flag) {
		log_info("Shutdown signal received, exiting gracefully");
		in_ml.quit = true;
	}
	
	return in_ml;
}

/* Callback: post-process input (nothing to do) */
static struct ML_result
post_process_input(struct ML_result in_ml, int bitpos)
{
	(void)bitpos;
	return in_ml;
}

int
main(int argc, char *argv[])
{
	struct json_object *config, *value;
	int res;
	
	(void)argc;
	(void)argv;
	
	log_info("dcf77pi-ntpsec daemon starting");
	
	/* Set up signal handlers for graceful shutdown */
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	
	/* Load configuration */
	config = json_object_from_file(ETCDIR "/config.json");
	if (config == NULL) {
		log_error("Could not open config file: %s/config.json", ETCDIR);
		log_error("Please ensure the config file exists and is readable");
		return EX_NOINPUT;
	}
	
	/* Get SHM unit number from config (optional, default 0) */
	if (json_object_object_get_ex(config, "shm_unit", &value)) {
		shm_unit = json_object_get_int(value);
		if (shm_unit < 0 || shm_unit > 3) {
			log_error("Invalid shm_unit %d (must be 0-3), using 0", shm_unit);
			shm_unit = 0;
		}
	}
	
	/* Note: outlogfile is not used by dcf77pi-ntpsec, only systemd journal logging */
	
	/* Initialize GPIO live mode */
	res = set_mode_live(config);
	json_object_put(config);
	
	if (res != 0) {
		log_error("set_mode_live() failed with error %d", res);
		cleanup();
		return res;
	}
	
	log_info("GPIO initialized successfully");
	
	/* Initialize NTPSec shared memory */
	res = init_shm();
	if (res != 0) {
		log_error("Failed to initialize NTPSec SHM");
		cleanup();
		return res;
	}
	
	log_info("Starting DCF77 decode loop");
	
	/* Run the main decode loop - no logfile, only journal logging */
	mainloop(NULL, get_bit_live, display_bit, display_long_minute,
	    display_minute, NULL /* no new_second callback */,
	    display_alarm, display_unknown, display_weather, display_time,
	    display_thirdparty_buffer, process_setclock_result,
	    process_input, post_process_input);
	
	/* Cleanup */
	log_info("Shutting down");
	cleanup_shm();
	cleanup();
	
	log_info("dcf77pi-ntpsec daemon stopped");
	return 0;
}
