# Windows WinUSB setup

`nfcidm.exe` uses SetupAPI and WinUSB for older Sony USB readers. It uses WinSCard for PC/SC readers. It does not install drivers.

| Reader | VID:PID | Transfer |
|---|---|---|
| RC-S320 | `054c:01bb` | Control OUT + Interrupt IN |
| RC-S330 / RC-S360 / RC-S370 | `054c:02e1` | Bulk OUT / IN |
| RC-S380/S | `054c:06c1` | Bulk OUT / IN |
| RC-S380/P | `054c:06c3` | Bulk OUT / IN |

To associate a Sony reader with WinUSB using [Zadig](https://zadig.akeo.ie/):

1. Connect one reader and run Zadig as administrator.
2. Select `Options > List All Devices` and verify its VID:PID.
3. Choose `Device > Load Preset Device` and load the corresponding `zadig/*.cfg`.
4. Select `WinUSB`, install the driver, and reconnect the reader if necessary.
5. Run `nfcidm.exe --list` and `nfcidm.exe --timeout 10 --verbose`.

Replacing a Sony vendor driver can prevent other Sony software from using the reader. Do not change working ACR122U or RC-S300 PC/SC drivers.

Check PnP devices in PowerShell:

```powershell
Get-PnpDevice -PresentOnly |
  Where-Object { $_.InstanceId -match 'VID_054C' } |
  Format-Table Status, Class, FriendlyName, InstanceId -AutoSize
```

PnP problem code 28 means that no driver is installed. WinUSB discovery requires a registered device-interface GUID, included in the Zadig presets. Errors go to stderr; on success stdout contains only an IDm, for example `0123456789ABCDEF`.
