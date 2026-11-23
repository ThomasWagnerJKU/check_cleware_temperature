# check_cleware_temperature

Nagios/Naemon/Icinga plugin for **Cleware USB temperature sensors** (USB-Temp, USB 0d50:0010, and the
related types 0x11/0x15/humidity), plus a converter for the old **Cleware Control** Windows log files
(`*.cwc`).

* `check_cleware_temp`: a small C++ program linked statically against Cleware's own
  [USBaccess library](https://www.cleware-shop.de/Software-E/Linux) (GPL-3, included unchanged in
  [`cleware/`](cleware/)). It is one static binary with no runtime dependencies.
* [`tools/cwc2influx.py`](tools/cwc2influx.py) converts Cleware Control 4.x `.cwc` logs to CSV or
  InfluxDB line protocol. The file format is described in
  [`docs/cwc-file-format.md`](docs/cwc-file-format.md).

## Build

```sh
make            # needs gcc, g++ and static glibc/libstdc++
make install    # -> /usr/local/lib/nagios/plugins/check_cleware_temp (PLUGINDIR=... to change)
```

On openSUSE install `glibc-devel-static`; on Debian, `build-essential` is enough.

## Usage

```
check_cleware_temp [-s serial] [-w range] [-c range] [-t timeout] [-d]
  -s serial   use the sensor with this serial number (decimal; default: first temperature sensor)
  -w/-c range warning/critical range in degC, Nagios range syntax ([@]start:end, ~ = -inf)
  -t seconds  timeout (default 20)
  -d          debug output
```

```
$ check_cleware_temp -s 1077 -w 29 -c 33
TEMP OK - 27.00 C (Cleware USB-Temp serial 1077) | temperature=27.00;29;33
```

The serial is the USB serial number string read as hex: `0000435` → 0x435 = 1077. Run with `-d`
to list all Cleware devices.

The sensor measures continuously. The plugin reads until the sensor reports a valid value. If none
arrives within about 5 s, it resets the sensor once and tries again; if that also fails, it returns
UNKNOWN.

## Permissions

The library uses `/dev/usb/hiddevN`, which is `root:root 0600` by default. Install the udev rule
[`contrib/70-cleware-usbtemp.rules`](contrib/70-cleware-usbtemp.rules) and adapt `GROUP=` to the
user your monitoring core runs as:

```sh
install -m 0644 contrib/70-cleware-usbtemp.rules /etc/udev/rules.d/
udevadm control --reload && udevadm trigger --subsystem-match=usbmisc --action=change
```

## Naemon / Nagios configuration

```
define command {
  command_name   check_cleware_temp
  command_line   $USER2$/check_cleware_temp -s $ARG1$ -w $ARG2$ -c $ARG3$
}

define service {
  service_description   Temperature Cleware USB-Temp
  host_name             myhost
  use                   generic-service
  check_command         check_cleware_temp!1077!29!33
  check_interval        2
  retry_interval        2
}
```

## Running in a VM

The sensor can be passed through to a libvirt guest by vendor/product ID. With
`startupPolicy='optional'` the VM still starts when the stick is missing:

```xml
<hostdev mode='subsystem' type='usb' managed='yes'>
  <source startupPolicy='optional'>
    <vendor id='0x0d50'/>
    <product id='0x0010'/>
  </source>
</hostdev>
```

`virsh attach-device <vm> cleware.xml --live --config`. Libvirt doesn't reattach a live hostdev by
itself after the stick has been unplugged and plugged in again. Run `attach-device --live` again,
or restart the VM.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). The files in [`cleware/`](cleware/) are © Cleware GmbH,
GPL-3, taken unchanged from `Linux_Ubuntu.zip` (USBaccess 6.6.3, 2023-08-02).
