// Copyright 2013-2018 René Ladan
// SPDX-License-Identifier: BSD-2-Clause

#include "decode_alarm.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char * const reg1n = "SWH, HH, NS, BR, MVP";
static const char * const reg1m = "NRW, SA, BRA, B, TH, S";
static const char * const reg1s = "RP, SAA, HS, BW, BYN, BYS";

void
decode_alarm(const unsigned civbuf[], struct alm * const alarm)
{
	/* Partial information only, no parity checks */

	for (unsigned i = 0; i < 2; i++) {
		alarm->region[i].r1 =
		    civbuf[6 * i] +
		    2 * civbuf[1 + 6 * i] +
		    4 * civbuf[3 + 6 * i];
		alarm->region[i].r2 =
		    civbuf[12 + 14 * i] +
		    2 * civbuf[13 + 14 * i] +
		    4 * civbuf[14 + 14 * i];
		alarm->region[i].r3 =
		    civbuf[15 + 14 * i] +
		    2 * civbuf[16 + 14 * i] +
		    4 * civbuf[17 + 14 * i];
		alarm->region[i].r4 =
		    civbuf[19 + 14 * i] +
		    2 * civbuf[20 + 14 * i] +
		    4 * civbuf[21 + 14 * i] +
		    8 * civbuf[23 + 14 * i];

		alarm->parity[i].ps =
		    civbuf[2 + 6 * i] +
		    2 * civbuf[4 + 6 * i] +
		    4 * civbuf[5 + 6 * i];
		alarm->parity[i].pl =
		    civbuf[18 + 14 * i] +
		    2 * civbuf[22 + 14 * i] +
		    4 * civbuf[24 + 14 * i] +
		    8 * civbuf[25 + 14 * i];
	}
}

const char * const
get_region_name(struct alm alarm)
{
	static char res[256]; /* Static buffer to avoid memory leak */
	size_t pos = 0;
	bool need_comma;

	/* Partial information only */

	if (alarm.region[0].r1 != alarm.region[1].r1 ||
	    alarm.parity[0].ps != alarm.parity[1].ps) {
		return "(inconsistent)";
	}

	res[0] = '\0'; /* Clear buffer */
	need_comma = false;

	if ((alarm.region[0].r1 & 1) == 1) {
		pos += snprintf(res + pos, sizeof(res) - pos, "%s", reg1n);
		need_comma = true;
	}
	if ((alarm.region[0].r1 & 2) == 2) {
		if (need_comma && pos < sizeof(res) - 3) {
			pos += snprintf(res + pos, sizeof(res) - pos, ", ");
		}
		if (pos < sizeof(res)) {
			pos += snprintf(res + pos, sizeof(res) - pos, "%s", reg1m);
		}
		need_comma = true;
	}
	if ((alarm.region[0].r1 & 4) == 4) {
		if (need_comma && pos < sizeof(res) - 3) {
			pos += snprintf(res + pos, sizeof(res) - pos, ", ");
		}
		if (pos < sizeof(res)) {
			snprintf(res + pos, sizeof(res) - pos, "%s", reg1s);
		}
	}
	return res;
}
