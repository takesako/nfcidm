# Compatibility

Physical-device results reported by the project maintainer in November 2026.
They have not been independently reproduced. "Confirmed" means that the
reader or its indicated model family produced a FeliCa IDm in a hands-on test.

| Reader | macOS (Apple Silicon) | Windows x64 | Linux / Alpine (v0.5.5) |
|---|---|---|---|
| Sony RC-S320 | Confirmed | Confirmed | Confirmed (libusb) |
| Sony RC-S330 family (`054c:02e1`) | Confirmed (RC-S330) | Confirmed (RC-S330) | Confirmed (libusb) |
| Sony RC-S380 (`054c:06c1` / `054c:06c3`) | Confirmed | Confirmed | Confirmed (libusb) |
| ACS ACR122U (`072f:2200`) | Confirmed | Confirmed | Confirmed (PC/SC) |
| Sony RC-S300 / RC-S300/S / RC-S300/S1 | Not individually recorded | Not individually recorded | PC/SC supported; individual variant not recorded |

The Linux `libusb-1.0` backend and the existing shared Sony polling/framing
code have been exercised with physical Sony readers; the ACR122U has also
been tested successfully through `pcscd` and the CCID driver. The RC-S330,
RC-S360, and RC-S370 share USB ID `054c:02e1` and the same backend. RC-S380
uses `054c:06c1` (S) or `054c:06c3` (P). Separate validation of every model
name and hardware revision is not implied by a shared-ID family test.

Linux Sony USB devices may require root or an explicit `/dev/bus/usb` access
rule (`LIBUSB_ERROR_ACCESS`). ACR122U/RC-S300 require a running PC/SC service.
If a VM disconnects its USB reader during initialization, `pcscd` can report
`LIBUSB_ERROR_NO_DEVICE`; reattach the device through the hypervisor's USB
menu and restart the service if needed. More details: [Linux setup](LINUX.md).

To troubleshoot, run `nfcidm --list` and `nfcidm --timeout 10 --verbose`.
