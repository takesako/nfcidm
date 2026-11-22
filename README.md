# nfcidm

`nfcidm` reads one FeliCa IDm and prints it as a hexadecimal string, for example:

```text
0123456789ABCDEF
```

The command is written in C and shares FeliCa framing/polling across platforms.
Sony USB readers use IOKit on macOS, WinUSB on Windows, and **libusb-1.0 on
Linux**. ACR122U and Sony RC-S300-series readers use PC/SC on every OS.
macOS and Windows builds do not require libusb or libnfc.

```sh
nfcidm                  # wait up to 10 seconds for one card
nfcidm --timeout 30     # wait up to 30 seconds
nfcidm --reader 2       # choose a reader from --list
nfcidm --list           # list supported readers
nfcidm --verbose        # diagnostics on stderr
```

On success, stdout contains only the IDm and a newline. Errors and diagnostics go to stderr; on failure, the exit code is nonzero and stdout is empty. `--list`, `--help`, and `--version` print their requested information to stdout.

Use `idm=$(nfcidm) || exit $?` in shell scripts. C programs can receive `$(nfcidm)` as an argument or read from `nfcidm | ./program`.

## Build

macOS: Apple Silicon, macOS 15 or later, Xcode Command Line Tools.

```sh
make
./nfcidm --list
./nfcidm
```

Windows: x64, MinGW-w64 GCC (including Strawberry Perl's GCC), and `make`.

```cmd
make CC=gcc
nfcidm.exe --list
nfcidm.exe --timeout 10
```

Linux (Alpine Linux):

```sh
sudo apk add build-base pkgconf pcsc-lite-dev libusb-dev pcsc-lite pcsc-tools ccid usbutils
git clone https://github.com/takesako/nfcidm.git
cd nfcidm
make
make test
./nfcidm --list
./nfcidm --timeout 10 --verbose
```

For verified Linux readers, USB access permissions, PC/SC services, and VM USB
passthrough, see [Linux setup and troubleshooting](docs/LINUX.md).

Legacy Sony USB readers on Windows require WinUSB; see
[Windows setup](docs/WINDOWS_WINUSB.md).

Run `make test` on macOS or a POSIX development host. Windows native unit tests are not configured; the shared protocols and WinUSB transport have mock tests.

The Homebrew Formula is maintained in the separate
[takesako/homebrew-nfcidm](https://github.com/takesako/homebrew-nfcidm) repository.
Install from that tap with `brew install takesako/nfcidm/nfcidm`.

## Supported readers

| Reader | USB VID:PID | macOS | Windows | Linux (v0.5.5) |
|---|---|---|---|---|
| Sony RC-S320 | `054c:01bb` | IOKit | WinUSB | libusb |
| Sony RC-S330 / RC-S360 / RC-S370 | `054c:02e1` | IOKit | WinUSB | libusb |
| Sony RC-S380 | `054c:06c1` / `054c:06c3` | IOKit | WinUSB | libusb |
| ACS ACR122U | `072f:2200` | PC/SC | WinSCard | pcsc-lite |
| Sony RC-S300 / RC-S300/S / RC-S300/S1 | PC/SC reader name | PC/SC | WinSCard | pcsc-lite |

RC-S320, RC-S330, RC-S380, and ACR122U were verified on macOS and Windows.
**Linux (v0.5.5) has also been tested with physical Sony readers and ACR122U
on Alpine Linux**, including successful FeliCa IDm output. The shared
RC-S330/360/370 VID:PID and RC-S380/S/P variants use the same protocol
backends; model names alone do not prove that every variant was tested
individually. See [compatibility](docs/COMPATIBILITY.md) for the reported
hardware coverage (November 2026).

See [protocol details](docs/protocol.md) for USB commands, framing, FeliCa polling, PC/SC handling, and references.

## License

MIT. See [LICENSE](LICENSE).
