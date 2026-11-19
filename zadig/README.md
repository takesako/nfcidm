# Zadig presets

Use `Device > Load Preset Device` in [Zadig](https://zadig.akeo.ie/) and verify the USB VID:PID before installing WinUSB.

| Preset | VID:PID |
|---|---|
| `winusb-sony-rc-s320.cfg` | `054c:01bb` |
| `winusb-sony-rc-s330-360-370.cfg` | `054c:02e1` |
| `winusb-sony-rc-s380-s.cfg` | `054c:06c1` |
| `winusb-sony-rc-s380-p.cfg` | `054c:06c3` |

These files do not install drivers. Do not replace the PC/SC drivers for ACR122U or RC-S300 models. See [Windows setup](../docs/WINDOWS_WINUSB.md).
