# Compatibility

Hardware results reported in November 2026. They have not been independently reproduced.

| Reader | macOS (Apple Silicon) | Windows x64 |
|---|---|---|
| Sony RC-S320 | Confirmed | Confirmed |
| Sony RC-S330 | Confirmed | Confirmed |
| Sony RC-S380 | Confirmed | Confirmed |
| ACS ACR122U | Confirmed | Confirmed |
| Sony RC-S360 / RC-S370 | Not tested | Not tested |
| Sony RC-S300 / RC-S300/S / RC-S300/S1 | Not tested | Not tested |

The RC-S330, RC-S360, and RC-S370 share USB ID `054c:02e1` and the same command backend. RC-S380 uses `054c:06c1` (S) or `054c:06c3` (P). Windows Sony USB readers require WinUSB; ACR122U uses PC/SC.

To troubleshoot, run `nfcidm --list` and `nfcidm --timeout 10 --verbose`.
