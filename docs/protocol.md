# NFC and FeliCa protocol

This document describes the USB and PC/SC communication used by `nfcidm`. The command polls for one FeliCa card, extracts its 8-byte IDm, and writes a string such as `0123456789ABCDEF` to stdout. It does not read or write card memory.

The program uses native macOS IOKit / PCSC.framework and Windows SetupAPI / WinUSB / WinSCard APIs. libusb, libnfc, nfcpy, and libpafe are not runtime dependencies.

## Reader selection

| Reader | VID:PID | Transport | Protocol |
|---|---|---|---|
| Sony RC-S320 | `054c:01bb` | USB control OUT + interrupt IN | Legacy normal frame |
| Sony RC-S330 / RC-S360 / RC-S370 | `054c:02e1` | USB bulk OUT / IN | RC-S956 (PN53x framing) |
| Sony RC-S380/S | `054c:06c1` | USB bulk OUT / IN | NFC Port-100 |
| Sony RC-S380/P | `054c:06c3` | USB bulk OUT / IN | NFC Port-100 |
| ACS ACR122U | `072f:2200` (reference) | PC/SC | ATR + Get Data APDU |
| Sony RC-S300 / RC-S300/S / RC-S300/S1 | Selected by PC/SC name | PC/SC | ATR + Get Data APDU |

USB readers are identified by their VID:PID and device instance. The RC-S330, RC-S360, and RC-S370 share a USB ID and backend. PC/SC returns reader names rather than USB IDs; its backend selects recognized PICC readers and excludes contact/SAM slots. An operating-system reader driver is required before a PC/SC reader can be used.

## USB frame format

`src/frame.c` reassembles protocol frames independently of USB transfer boundaries. A single USB read may contain an ACK and data, or a frame may span several reads.

| Frame | Bytes |
|---|---|
| Normal | `00 00 FF LEN LCS DATA... DCS 00` |
| PN53x extended | `00 00 FF FF FF LEN_H LEN_L LCS DATA... DCS 00` |
| Port-100 extended | `00 00 FF FF FF LEN_L LEN_H LCS DATA... DCS 00` |
| ACK | `00 00 FF 00 FF 00` |
| NACK | `00 00 FF FF 00 00` |

`LCS` complements the length field, and `DCS` complements the data bytes: each byte sum is zero modulo 256. Extended-length byte order differs between PN53x and Port-100. The parser verifies length, buffer capacity, checksums, and trailing `00` before returning a payload.

## Sony RC-S320

Implemented in `src/sony_legacy.c` (`nfc_s320_*`). Output uses a vendor/device control request with `bRequest = 0`, `wValue = 0`, and `wIndex = 0`. Input uses interrupt IN. Bytes below are command payloads before framing.

| Step | Command payload | Purpose |
|---|---|---|
| Initialize 1 | `62 01 82` | Reader register setup |
| Initialize 2 | `62 02 80 81` | Reader register setup |
| Initialize 3 | `62 22 80 CC 81 88` | Reader register setup |
| Initialize 4 | `62 02 80 81` | Reader register setup |
| Initialize 5 | `62 02 82 87` | Reader register setup |
| Initialize 6 | `62 21 25 58` | Reader register setup |
| Initialize 7 | `5A 80` | Reader setup |
| Poll | `5C 06 00 FF FF 00 00` | FeliCa Polling |
| Close | `54` | Reader cleanup |

The `62` commands configure the reader; they do not write to the card. The Polling command selects system code `FFFF`, request code `00`, and time slot `00`. The response is checked for `5D`, its length, FeliCa response code `01`, IDm (8 bytes), and PMm (8 bytes). The parser accepts the two observed length conventions but requires the complete card response.

When a command was acknowledged but no data frame followed, the polling loop retries. An unacknowledged command, a partial frame, or a USB transfer timeout is treated as an error rather than silently reported as “no card.”

## Sony RC-S330 / RC-S360 / RC-S370

Implemented in `src/sony_legacy.c` (`nfc_s330_*`). These readers share the RC-S956 command set, PN53x frames, bulk IN/OUT transfers, and command direction bytes `D4` (request) / `D5` (response).

| Step | Command payload | Purpose |
|---|---|---|
| Before initialization | Host ACK | Abort an unfinished prior command |
| ResetMode | `D4 18 01` | Reset the reader state |
| RF off | `D4 32 01 00` | Disable RF |
| Retry settings | `D4 32 05 00 00 01` | Bound automatic retries |
| RF on | `D4 32 01 01` | Enable RF |
| Poll | `D4 4A 01 01 00 FF FF 00 00` | `InListPassiveTarget`, one NFC-F 212 kbit/s target |

After `ResetMode`, the host sends an ACK and waits 10 ms. The polling response starts with `D5 4B`, followed by target count, target number, FeliCa response length, response code `01`, IDm (8 bytes), PMm (8 bytes), and optionally a system code. Only complete responses with the expected lengths are accepted.

If polling is acknowledged but returns no data, recovery sends a host ACK, drains buffered data, resets the reader, turns RF off/on, and waits 10 ms before trying again. Each recovery operation has a finite timeout. Closing attempts to abort a pending command, reset state, and turn RF off.

## Sony RC-S380/S and RC-S380/P

Implemented in `src/sony_rcs380.c`. These readers use NFC Port-100 frames with `D6` for requests and `D7` for responses. The response command number must equal the request command number plus one.

| Command | Payload | Purpose |
|---|---|---|
| SetCommandType | `D6 2A 01` | Select command type 1 |
| SwitchRF | `D6 06 00` | Disable RF |
| InSetRF | `D6 00 01 01 0F 01` | NFC-F 212 kbit/s transmit/receive configuration |
| InSetProtocol | `D6 02` + parameter/value pairs | Configure CRC, guard times, receive mode, etc. |
| InCommRF | `D6 04 6E 00 06 00 FF FF 00 00` | FeliCa Polling |

`InSetProtocol` explicitly configures the settings defined in the `protocol` array in `src/sony_rcs380.c`, rather than relying on values left by a previous application. The InCommRF receive timeout is `6E 00` (110 units of 0.1 ms, or 11 ms), distinct from the USB I/O timeout.

The reply begins with a 4-byte little-endian RF status. Status `0x00000080` means the RF receive operation timed out and polling may retry; other nonzero RF statuses are errors. A successful reply requires the collision-information byte and a complete FeliCa response of length 18, response code `01`, IDm (8 bytes), and PMm (8 bytes). Closing attempts to switch RF off and send a host ACK.

## PC/SC: ACR122U and Sony RC-S300 series

Implemented in `src/pcsc.c` and `src/pcsc_card.c`. PC/SC communication follows this sequence:

```text
SCardListReaders
    -> SCardGetStatusChange
    -> SCardConnect (SCARD_SHARE_SHARED)
    -> SCardStatus (ATR)
    -> check FeliCa ATR
    -> SCardTransmit (Get Data)
    -> SCardDisconnect (SCARD_LEAVE_CARD)
```

The program accepts a FeliCa storage-card ATR, not merely any card with an 8-byte UID. The expected format is 20 bytes, including the prefix `3B 8F 80 01 80 4F 0C A0 00 00 03 06`, the card identification bytes, and a valid XOR check byte (TCK). Recognized identification bytes include:

| Identification | Meaning |
|---|---|
| `03 F0 11` | FeliCa at 212 kbit/s |
| `03 F0 12` | FeliCa at 424 kbit/s |
| `11 00 3B` | Alternate FeliCa identifier |

Only after the FeliCa ATR check does the program send the APDU:

```text
FF CA 00 00 00
```

A successful response is exactly `IDm[8] 90 00` (10 bytes). The program preserves the byte order returned by the reader and prints the eight bytes as hexadecimal characters. It does not label a non-FeliCa card UID as an IDm. RC-S300 variants have not been individually verified with hardware.

PC/SC operations use native ABI types (`LONG`, `DWORD`) for each OS. `SCardGetStatusChange` uses short finite waits; `SCardTransmit` has no per-call timeout argument, so the CLI also maintains its own deadline and exit behavior.

## Native USB backends

### macOS (IOKit)

`src/platform/macos/usb.c` enumerates IORegistry USB devices, obtains a device and interface through `IOCreatePlugInInterfaceForService` and `QueryInterface`, and examines endpoint directions and types with `GetPipeProperties`.

- Bulk transfers use `ReadPipeTO` / `WritePipeTO` with bounded timeouts.
- Vendor control transfers use `DeviceRequestTO` and `IOUSBDevRequestTO`.
- RC-S320 interrupt reads use `ReadPipeAsync` plus a CoreFoundation run loop.
- Timed-out interrupt requests are aborted with `AbortPipe`; callback data stays allocated until completion is observed.

### Windows (SetupAPI and WinUSB)

`src/platform/windows/usb_winusb.c` enumerates USB PnP instances, matches supported Sony VID:PID values, and looks up the registered WinUSB device-interface GUID. The instance ID identifies the correct interface even when several readers share the same GUID. `CreateFile` / `WinUsb_Initialize` open the device; the USB pipe types determine the transfer path.

- RC-S330 and RC-S380 use WinUSB bulk IN/OUT endpoints.
- RC-S320 uses a vendor control OUT request and an interrupt IN endpoint.
- USB I/O is bounded; incomplete or failed transfers are reported as errors.
- WinUSB requires an appropriate Windows driver binding and a registered interface GUID. See [Windows setup](WINDOWS_WINUSB.md).

## IDm output and error handling

A successful poll produces one 8-byte IDm. The CLI formats it as 16 uppercase hexadecimal digits followed by a newline, for example `0123456789ABCDEF`. Only the IDm goes to stdout; errors and optional tracing go to stderr. A missing card is retried until the requested timeout; corrupted frames and disconnected readers fail rather than producing partial IDm output.

## References

- [nfcpy (Sony reader and NFC protocol implementations)](https://github.com/nfcpy/nfcpy)
- [libpafe (legacy Sony PaSoRi)](https://github.com/htrb/libpafe)
- [PaSoRich (RC-S320 implementation)](https://github.com/con3code/xcx-pasorich)
- [Linux NFC Port-100 driver (protocol reference)](https://github.com/torvalds/linux/blob/master/drivers/nfc/port100.c)
- [ACS ACR122U API manual](https://downloads.acs.com.hk/drivers/en/API-ACR122U-2.03.pdf)
- [ACS ACR1555U reference manual (FeliCa ATR identifier)](https://www.acs.com.hk/download-manual/14203/REF-ACR1555U-1.08-EN.pdf)
- [Apple IOUSBLib interface](https://github.com/apple-oss-distributions/IOUSBFamily/blob/main/IOUSBFamily/Headers/IOUSBLib.h)
- [PC/SC API reference](https://pcsclite.apdu.fr/api/group__API.html)
- [Sony PaSoRi products](https://www.sony.co.jp/Products/felica/)
