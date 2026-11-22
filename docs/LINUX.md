# Linux setup and troubleshooting

Linux support was added in nfcidm v0.5.5. Physical-reader FeliCa IDm reads
have been verified on Alpine Linux with Sony USB readers using `libusb-1.0`
and ACS ACR122U using PC/SC (November 2026).

The Linux backends use different transports:

- **Sony RC-S320/330/360/370/380:** direct libusb-1.0; `pcscd` is not
  required. Listing a USB reader does not guarantee permission to open it.
- **ACS ACR122U and Sony RC-S300 family:** PC/SC (`pcscd` and a supported
  CCID driver), not the Sony libusb transport. Start `pcscd` before reading.

The commands below use `sudo`. If it is missing from a minimal Alpine image,
install it as root with `apk add sudo` and grant the account appropriate
`sudo` privileges before running the commands.

## Dependencies and build (Alpine Linux)

```sh
sudo apk add build-base pkgconf libusb-dev pcsc-lite-dev \
  pcsc-lite ccid pcsc-tools usbutils
make
make test
./nfcidm --version
```

`libusb-dev` is the Alpine package for **libusb-1.0**. The separate
`libusb-compat-dev` package is not needed. `make` links both libusb and
PC/SC into the Linux binary; `make test` also exercises mocked transports.

## Sony PaSoRi: direct USB via libusb

For RC-S320 (`054c:01bb`), RC-S330/360/370 (`054c:02e1`), and RC-S380/S/P
(`054c:06c1` and `054c:06c3`), **`pcscd` is not required**.

```sh
lsusb -d 054c:02e1        # example Sony reader
./nfcidm --list
./nfcidm --timeout 10 --verbose
```

`--list` enumerates USB devices without necessarily opening them. If the
reader appears but `nfcidm` prints:

```text
nfcidm: open USB reader (check /dev/bus/usb permissions): LIBUSB_ERROR_ACCESS (-3)
```

then test whether Linux USB permissions are the issue:

```sh
sudo ./nfcidm --timeout 10 --verbose
```

For a temporary local test, find the current bus/device numbers with `lsusb`.
For example, `Bus 001 Device 005` corresponds to `/dev/bus/usb/001/005`:

```sh
ls -l /dev/bus/usb/001/005
sudo chmod 666 /dev/bus/usb/001/005   # test only; temporary and permissive
./nfcidm
```

This change disappears when the device is re-enumerated. Do **not** set the
setuid bit on `nfcidm` or `lsusb`. For a persistent and more restrictive
solution on a system with **udev/eudev running**, create a dedicated group and
rules restricted to the Sony reader VID/PIDs:

```sh
sudo addgroup -S nfcusb
sudo addgroup "$(id -un)" nfcusb
sudo mkdir -p /etc/udev/rules.d
cat <<'EOF' | sudo tee /etc/udev/rules.d/70-nfcidm-sony.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="054c", ATTR{idProduct}=="01bb", GROUP="nfcusb", MODE="0660"
SUBSYSTEM=="usb", ATTR{idVendor}=="054c", ATTR{idProduct}=="02e1", GROUP="nfcusb", MODE="0660"
SUBSYSTEM=="usb", ATTR{idVendor}=="054c", ATTR{idProduct}=="06c1", GROUP="nfcusb", MODE="0660"
SUBSYSTEM=="usb", ATTR{idVendor}=="054c", ATTR{idProduct}=="06c3", GROUP="nfcusb", MODE="0660"
EOF
```

Then reload rules using your udev/eudev service, replug the reader, and log
in again so the supplementary group applies. **Minimal Alpine installations
may use mdev instead of eudev**, in which case udev rules will have no effect;
use an appropriate mdev policy or the `sudo` workaround for development.

## ACR122U and Sony RC-S300: PC/SC

The ACS ACR122U (`072f:2200`) and the Sony RC-S300 family use PC/SC rather
than the Sony libusb transport. Install `pcsc-lite`, the `ccid` driver, and
`pcsc-tools`, then start the daemon if it is not already running:

```sh
lsusb -d 072f:2200      # example ACR122U

sudo pcscd              # only if the service is not already running
pcsc_scan               # Ctrl+C to stop scanning
./nfcidm --list
./nfcidm --timeout 10 --verbose
```

On conventional OpenRC systems, `sudo rc-service pcscd start` can manage the
daemon (after `rc-update add pcscd default`). On a minimal QEMU guest with a
custom init instead of OpenRC as PID 1, `rc-service` may fail with
`failed to acquire lock: Bad file descriptor`. Use `sudo pcscd` directly in
that environment. For detailed diagnostics, stop an already running `pcscd`
and start it in the foreground:

```sh
sudo killall pcscd
sudo pcscd --foreground --debug
```

Run `pcsc_scan` in another terminal. A `Waiting for the first reader...`
message means that PC/SC has not registered a reader yet, even if `lsusb`
shows the device. It is not a card-polling failure in `nfcidm`.

## VirtualBox / VMware / QEMU USB passthrough

The guest must first see the reader with `lsusb`. VirtualBox and VMware can
pass a USB device through from a Windows host without changing the host's
ACR122U CCID driver to WinUSB. During passthrough, the device is typically
unavailable to applications on the Windows host.

Legacy Sony USB readers require WinUSB when used **directly on Windows**;
see [Windows setup](WINDOWS_WINUSB.md). Do not replace the Windows PC/SC
(CCID) drivers for ACR122U or Sony RC-S300 just to run `nfcidm`. With
VirtualBox/VMware USB passthrough, the **Linux guest** uses libusb-1.0 for
legacy Sony readers; binding the Windows host reader to WinUSB is not a
prerequisite for the Linux guest.

In one Alpine/VirtualBox ACR122U test, `pcscd` recognized `072f:2200` but
reader initialization did not finish. Its CCID logs subsequently reported:

```text
WriteUSB(): LIBUSB_ERROR_NO_DEVICE
InterruptRead(): LIBUSB_ERROR_NO_DEVICE
```

This means the USB device disappeared from the driver's perspective, not
simply that the FeliCa card was absent. Releasing and reconnecting the reader
through the VM's USB Devices menu, then restarting `pcscd`, restored reader
discovery and allowed `nfcidm` to print a valid IDm. USB bus/device numbers
can change on reconnect; do not hard-code them into application code.

If this happens repeatedly, inspect the VM's USB controller configuration
(xHCI is worth trying in VirtualBox) and the physical USB connection. Changing
`nfcidm`'s polling timeout does not repair a device that has disappeared.

If `LIBUSB_ERROR_BUSY` occurs, check whether another process or driver owns
the USB interface. Avoid running PC/SC and a direct USB driver against the
same interface simultaneously.

For supported devices and test coverage, see [compatibility](COMPATIBILITY.md).
