# Changelog

## 0.5.5

- Add a native Linux `libusb-1.0` USB transport for Sony RC-S320,
  RC-S330/360/370, and RC-S380/S/P. Reuse existing Sony framing/polling.
- Unify Sony USB VID/PID-to-protocol mapping across macOS, Windows, Linux.
- Select USB interfaces/endpoints from descriptors, support multiple readers
  with identical VID/PID, and map USB timeout/disconnection errors.
- Keep ACS ACR122U and Sony RC-S300 on PC/SC, unchanged.
- Add mock-based Linux USB transport tests (no NFC reader required).
- Keep the Homebrew Formula in the separate `takesako/homebrew-nfcidm`
  repository; remove the bundled Formula and generation helper.
- Confirm Linux IDm reading on physical Sony USB readers (libusb) and
  ACS ACR122U (PC/SC); document Alpine setup, USB permissions, and
  VM reconnection troubleshooting.

## 0.5.4

- Baseline release for the Linux USB backend changes.
