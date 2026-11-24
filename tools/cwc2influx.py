#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert Cleware Control temperature logs (*.cwc, Windows Cleware Control 4.x) to CSV or
InfluxDB line protocol. File format: see docs/cwc-file-format.md.

  cwc2influx.py [--format csv|influx] [--measurement M] [--tag k=v ...] [--field F] FILE|DIR ...

Records of all given files are merged, sorted by time and de-duplicated (same second).
Failed readings (9999.0) are skipped. Diagnostics go to stderr.
"""
import argparse
import datetime
import glob
import os
import struct
import sys

MAGIC = 0x340          # first uint32 of every file seen (Cleware Control 4.x)
ERR = 9999.0           # Cleware Control's value for a failed reading / lost sensor
TMIN, TMAX = 1262304000, 2147483647   # plausible timestamps: 2010-01-01 .. 2038-01-19


def read_cwc(path):
    """Return header info and records [(unix_time, degC), ...] of one .cwc file."""
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 0x40:
        raise ValueError('file too short')
    magic, devtype, version, serial = struct.unpack_from('<4I', d, 0x00)
    if magic != MAGIC:
        raise ValueError(f'unknown magic 0x{magic:x}')
    count, tmin, tmax = struct.unpack_from('<Iff', d, 0x30)
    offset = len(d) - 8 * count           # data block = last 8*count bytes
    if offset < 0x40:
        raise ValueError(f'record count {count} does not fit the file size {len(d)}')
    recs = []
    for i in range(count):
        t, ts = struct.unpack_from('<fI', d, offset + 8 * i)
        if not TMIN <= ts <= TMAX:
            raise ValueError(f'implausible timestamp {ts} in record {i}')
        recs.append((ts, t))
    return {'serial': serial, 'devtype': devtype, 'version': version, 'count': count,
            'tmin': tmin, 'tmax': tmax, 'offset': offset, 'records': recs}


def esc(s, chars):
    for c in '\\' + chars:
        s = s.replace(c, '\\' + c)
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('paths', nargs='+', help='.cwc files or directories containing them')
    ap.add_argument('--format', choices=('csv', 'influx'), default='csv')
    ap.add_argument('--measurement', default='temperature')
    ap.add_argument('--tag', action='append', default=[], metavar='K=V', help='tag for line protocol (repeatable)')
    ap.add_argument('--field', default='value', help='field name for line protocol (default: value)')
    a = ap.parse_args()

    files = []
    for p in a.paths:
        files += sorted(glob.glob(os.path.join(p, '*.cwc'))) if os.path.isdir(p) else [p]
    pts, errors = {}, 0
    for p in files:
        try:
            h = read_cwc(p)
        except (OSError, ValueError) as e:
            print(f'{p}: skipped: {e}', file=sys.stderr)
            continue
        for ts, t in h['records']:
            if t == ERR:
                errors += 1
            else:
                pts[ts] = t
        print(f"{os.path.basename(p)}: serial {h['serial']} type 0x{h['devtype']:x} records {h['count']} "
              f"data offset {h['offset']}", file=sys.stderr)
    print(f'{len(files)} files, {len(pts)} points, {errors} failed readings skipped', file=sys.stderr)

    out = sys.stdout
    if a.format == 'csv':
        out.write('time_utc,unix_time,temperature_degC\n')
        for ts in sorted(pts):
            iso = datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
            out.write(f'{iso},{ts},{pts[ts]:g}\n')
    else:
        key = esc(a.measurement, ', ')
        for kv in sorted(a.tag):
            k, _, v = kv.partition('=')
            key += f",{esc(k, ',= ')}={esc(v, ',= ')}"
        field = esc(a.field, ',= ')
        for ts in sorted(pts):
            out.write(f'{key} {field}={pts[ts]:g} {ts}\n')   # write with precision=s


if __name__ == '__main__':
    main()
