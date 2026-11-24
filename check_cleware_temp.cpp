// check_cleware_temp - Nagios/Naemon plugin for Cleware USB temperature sensors
// (USB-Temp 0x10, USB-Temp2 0x11, ...), using Cleware's USBaccess library (GPL-3,
// https://www.cleware-shop.de/Software-E/Linux, Linux_Ubuntu.zip), statically linked.
// Access via /dev/usb/hiddevN (read+write permission needed, see contrib/70-cleware-usbtemp.rules).
// https://github.com/ThomasWagnerJKU/check_cleware_temperature
//
// check_cleware_temp [-s serial] [-w range] [-c range] [-t timeout] [-d]
// Ranges use Nagios plugin syntax: [@]start:end, "~" = -infinity, "N" = 0:N
// (so an upper limit alone is "~:N", else temperatures below 0 alert too).
//
// SPDX-License-Identifier: GPL-3.0-only (like Cleware's USBaccess, which it links)
// Copyright (C) 2026 Thomas Wagner

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include "USBaccess.h"

enum { OK = 0, WARNING = 1, CRITICAL = 2, UNKNOWN = 3 };
static const char *stateName[] = { "OK", "WARNING", "CRITICAL", "UNKNOWN" };

struct Range {
	bool set = false, inside = false;
	double lo = 0, hi = INFINITY;
	const char *text = "";
};

static bool parseRange(const char *s, Range &r) {
	r.set = true; r.text = s; r.inside = false; r.lo = 0; r.hi = INFINITY;
	if (*s == '@') { r.inside = true; s++; }
	const char *colon = strchr(s, ':');
	char *end;
	if (!colon) {
		r.hi = strtod(s, &end);
		return end != s && *end == 0 && r.lo <= r.hi;
	}
	if (s[0] == '~' && s + 1 == colon) r.lo = -INFINITY;
	else {
		r.lo = strtod(s, &end);
		if (end == s || end != colon) return false;
	}
	if (colon[1] != 0) {
		r.hi = strtod(colon + 1, &end);
		if (*end != 0) return false;
	}
	return r.lo <= r.hi;
}

// true = alert
static bool checkRange(const Range &r, double v) {
	if (!r.set) return false;
	bool in = v >= r.lo && v <= r.hi;
	return r.inside ? in : !in;
}

static void onAlarm(int) {
	static const char msg[] = "TEMP UNKNOWN - plugin timed out\n";
	if (write(1, msg, sizeof msg - 1) < 0) {}
	_exit(UNKNOWN);
}

// a whole number from min to max, or false
static bool parseInt(const char *s, long min, long max, int &v) {
	char *end;
	long l = strtol(s, &end, 10);
	if (end == s || *end != 0 || l < min || l > max) return false;
	v = (int)l;
	return true;
}

static void usage() {
	printf("check_cleware_temp [-s serial] [-w range] [-c range] [-t timeout] [-d]\n"
	       "  -s serial   use the sensor with this serial number (default: first temperature sensor)\n"
	       "  -w/-c range warning/critical range in degC, Nagios syntax ([@]start:end, ~ = -inf);\n"
	       "              N alone means 0:N, so use ~:N for only an upper limit\n"
	       "  -t seconds  timeout (default 20)\n"
	       "  -d          debug output\n");
}

int main(int argc, char *argv[]) {
	int serial = -1, timeout = 20, debug = 0, opt;
	Range warn, crit;
	while ((opt = getopt(argc, argv, "s:w:c:t:dh")) != -1) {
		switch (opt) {
		case 's': if (!parseInt(optarg, 0, 0x7fffffff, serial)) { printf("TEMP UNKNOWN - invalid serial '%s'\n", optarg); return UNKNOWN; } break;
		case 'w': if (!parseRange(optarg, warn)) { printf("TEMP UNKNOWN - invalid warning range '%s'\n", optarg); return UNKNOWN; } break;
		case 'c': if (!parseRange(optarg, crit)) { printf("TEMP UNKNOWN - invalid critical range '%s'\n", optarg); return UNKNOWN; } break;
		case 't': if (!parseInt(optarg, 1, 3600, timeout)) { printf("TEMP UNKNOWN - invalid timeout '%s'\n", optarg); return UNKNOWN; } break;
		case 'd': debug = 1; break;
		default: usage(); return UNKNOWN;
		}
	}
	signal(SIGALRM, onAlarm);
	alarm(timeout);

	CUSBaccess cw;
	int count = cw.OpenCleware();
	if (debug) printf("OpenCleware: %d device(s)\n", count);
	int dev = -1;
	for (int i = 0; i < count; i++) {
		int type = cw.GetUSBType(i);
		int sn = cw.GetSerialNumber(i);
		if (debug) printf("device %d: type 0x%02x serial %d version %d\n", i, type, sn, cw.GetVersion(i));
		if (type != CUSBaccess::TEMPERATURE_DEVICE && type != CUSBaccess::TEMPERATURE2_DEVICE
		    && type != CUSBaccess::TEMPERATURE5_DEVICE && type != CUSBaccess::HUMIDITY1_DEVICE)
			continue;
		if (serial < 0 || serial == sn) { dev = i; break; }
	}
	if (dev < 0) {
		if (serial >= 0) printf("TEMP UNKNOWN - no Cleware temperature sensor with serial %d found (%d Cleware device(s), check /dev/usb/hiddev* permissions)\n", serial, count);
		else printf("TEMP UNKNOWN - no Cleware temperature sensor found (%d Cleware device(s), check /dev/usb/hiddev* permissions)\n", count);
		cw.CloseCleware();
		return UNKNOWN;
	}
	int sn = cw.GetSerialNumber(dev);

	// The sensor measures continuously and sets a valid bit; read until a valid value
	// arrives, reset the sensor once if it does not (like Cleware's USBtemp). The
	// library checks the valid bit, so 0.0 is a real reading.
	double t = 0; int timeID = 0; bool ok = false;
	for (int attempt = 0; attempt < 2 && !ok; attempt++) {
		if (attempt) {
			if (debug) printf("no valid value, resetting sensor\n");
			cw.ResetDevice(dev);
			usleep(2500 * 1000);
		}
		for (int retry = 0; retry < 20; retry++) {
			if (cw.GetTemperature(dev, &t, &timeID)) { ok = true; break; }
			if (debug) printf("GetTemperature failed (retry %d)\n", retry);
			usleep(250 * 1000);
		}
	}
	cw.CloseCleware();
	if (!ok) {
		printf("TEMP UNKNOWN - Cleware sensor %d returned no valid temperature\n", sn);
		return UNKNOWN;
	}
	if (debug) printf("temperature %.4f timeID %d\n", t, timeID);

	int state = checkRange(crit, t) ? CRITICAL : checkRange(warn, t) ? WARNING : OK;
	printf("TEMP %s - %.2f C (Cleware USB-Temp serial %d) | temperature=%.2f;%s;%s\n",
	       stateName[state], t, sn, t, warn.text, crit.text);
	return state;
}
