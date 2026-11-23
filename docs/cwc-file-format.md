# Cleware Control `.cwc` log file format

This describes the temperature log files (`log_DD.MM.YYYY HH.MM.SS_N.cwc`, `Kurve_*.cwc`) that the
Windows program **Cleware Control 4.4.3** writes for a Cleware **USB-Temp** sensor. Cleware does not
publish the format. It was worked out from 165 log files written between 2015-04 and 2026-11 with
one sensor (USB-Temp, device type 0x10, serial 1077). Every rule below holds for all of them.
[`tools/cwc2influx.py`](../tools/cwc2influx.py) implements it.

`file(1)` reports these files as `GLS_BINARY_LSB_FIRST`, which is a false match on the first bytes.

## Layout

```
+--------------------------------+  offset 0
| header (variable length)       |  fixed part 0x00..0x3F, then sensor + alarm settings
+--------------------------------+  offset = file size - 8 * record_count
| record 0                       |  8 bytes
| record 1                       |
| ...                            |
| record record_count-1          |  last 8 bytes of the file
+--------------------------------+
```

All values are little-endian.

## Header

Fixed part:

| Offset | Type    | Meaning                                                                  | Example |
|-------:|---------|--------------------------------------------------------------------------|---------|
| 0x00   | uint32  | magic/format, always `0x340`                                              | 0x340 |
| 0x04   | uint32  | Cleware device type (`0x10` = USB-Temp, see `USBaccessDevTypes.h`)       | 0x10 |
| 0x08   | uint32  | device firmware version                                                   | 5 |
| 0x0C   | uint32  | serial number of the sensor                                               | 1077 (0x435) |
| 0x10   | -       | in-memory pointers and settings, not needed for parsing                   | |
| 0x20   | uint32  | probably the sampling/display interval in ms (60000 in newer files, 2000 in the very first ones) | 60000 |
| **0x30** | **uint32** | **record_count**: number of 8-byte records at the end of the file | 35131 |
| 0x34   | float32 | minimum temperature of all records (°C)                                   | 24.9375 |
| 0x38   | float32 | maximum temperature of all records (°C, excluding 9999)                   | 29.0625 |
| 0x3C   | uint32  | number of alarm entries                                                   | 5 |

After the fixed part come the serial as a length-prefixed string (`04 "1077"`) and the alarm
definitions. Each alarm has a threshold (float32), a range pair −200.0/+200.0, flags, and two
length-prefixed (1-byte length, Latin-1) strings: the action (a mail command line or a `.bat` path)
and a message text. Mail addresses appear here in clear text, so take care when sharing header dumps.
In the analysed files the alarms were:

| Threshold   | Action |
|-------------|--------|
| 29.0 °C     | mail "It's getting hot in the server room!" (two entries, two recipients) |
| 9999.0      | mail "Cleware: Connection to Temperature Sensor lost" |
| 33.0 °C     | `C:\Cleware\EmergencyShutdown.bat` |
| 26.0 °C     | `C:\Cleware\WakeUpServer.bat` (with 0.25 hysteresis) |

The header length therefore depends on the alarm configuration: 698, 706, 857 and others were seen.
**Don't search for the data block. Compute it:**

```
data_offset = file_size - 8 * record_count      # record_count = uint32 at 0x30
```

For all 165 files this offset was correct. The parsed records also matched the count and the
min/max at 0x34/0x38. Files that contain only a header have `record_count = 0` (logging started
and stopped right away).

## Records

```
struct record {            // 8 bytes, little-endian
    float32 temperature;   // degrees Celsius, resolution 1/16 °C (0.0625)
    uint32  time;          // Unix time (seconds since 1970-01-01 00:00 UTC)
};
```

* `time` is true UTC epoch time, not local time. The first record of `log_05.10.2026 14.30.15_161.cwc`
  is 1791203421 = 2026-10-05 12:30:21 UTC = 14:30:21 CEST, 6 s after the start time in the file name,
  which is local time.
* Records are in ascending time order. The interval is the logging interval: 120 s (sometimes
  121/131/132 s) in this data set, and 2 s in the very first 2015 test logs.
* `temperature == 9999.0` marks a **failed reading**, e.g. when the sensor was lost. It shows up once
  in the data set (2019-05-16 14:48 UTC) and must be skipped, not plotted.
* Consecutive log files don't overlap. Each file starts where the program (re)started logging.

## Minimal parser (Python)

```python
import struct

def read_cwc(path):
    d = open(path, 'rb').read()
    magic, devtype, version, serial = struct.unpack_from('<4I', d, 0)
    assert magic == 0x340
    count = struct.unpack_from('<I', d, 0x30)[0]
    off = len(d) - 8 * count
    for i in range(count):
        temp, ts = struct.unpack_from('<fI', d, off + 8 * i)
        if temp != 9999.0:
            yield ts, temp          # (unix time UTC, °C)
```

## Converting

```sh
# CSV
tools/cwc2influx.py --format csv /path/to/log > cleware.csv

# InfluxDB 1.x, same series as check_cleware_temp via nagflux (OMD)
tools/cwc2influx.py --format influx --measurement metrics \
    --tag command=check_cleware_temp --tag crit-fill=none --tag warn-fill=none \
    --tag host=myhost --tag performanceLabel=temperature \
    --tag "service=Temperature Cleware USB-Temp" /path/to/log > cleware.lp
curl -u USER:PASS --data-binary @cleware.lp 'http://127.0.0.1:8087/write?db=nagflux&precision=s'
```

When backfilling years of data into InfluxDB 1.x, first set a long shard group duration, as
InfluxDB's documentation recommends (`ALTER RETENTION POLICY "autogen" ON "nagflux" SHARD DURATION 52w`).
Otherwise one shard per week is created. For large imports, split the file into chunks of about
50 000 lines.
