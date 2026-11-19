# nfcidm

`nfcidm` reads one FeliCa IDm and prints it as a hexadecimal string, for example:

```text
0123456789ABCDEF
```

The command is written in C and uses the native USB and PC/SC APIs on macOS and Windows. It does not require libusb or libnfc.

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

Legacy Sony readers on Windows require a WinUSB driver binding; follow [Windows setup](docs/WINDOWS_WINUSB.md). Leave the existing ACR122U and RC-S300 PC/SC drivers unchanged.

Run `make test` on macOS or a POSIX development host. Windows native unit tests are not configured; the shared protocols and WinUSB transport have mock tests.

For local Homebrew testing on Apple Silicon, use `sh scripts/install-local.sh`. For a GitHub release, use `make dist` and [the formula generator](scripts/formula.sh).

## Supported readers

| Reader | USB VID:PID | macOS | Windows |
|---|---|---|---|
| Sony RC-S320 | `054c:01bb` | IOKit | WinUSB |
| Sony RC-S330 / RC-S360 / RC-S370 | `054c:02e1` | IOKit | WinUSB |
| Sony RC-S380 | `054c:06c1` / `054c:06c3` | IOKit | WinUSB |
| ACS ACR122U | `072f:2200` | PC/SC | WinSCard |
| Sony RC-S300 / RC-S300/S / RC-S300/S1 | PC/SC reader name | PC/SC | WinSCard |

RC-S320, RC-S330, RC-S380, and ACR122U have been tested with physical readers on macOS and Windows (November 2026). Other listed Sony models have not been individually tested. See [compatibility](docs/COMPATIBILITY.md).

See [protocol details](docs/protocol.md) for USB commands, framing, FeliCa polling, PC/SC handling, and references.

## Release

Create a source archive with `make dist` and upload `dist/nfcidm-0.5.2.tar.gz` to a GitHub Release tagged `v0.5.2`. To publish a Homebrew formula in a separate tap, generate it with:

```sh
sh scripts/formula.sh dist/nfcidm-0.5.2.tar.gz \
  https://github.com/OWNER/nfcidm/releases/download/v0.5.2/nfcidm-0.5.2.tar.gz \
  https://github.com/OWNER/nfcidm > nfcidm.rb
```

Replace `OWNER` with the GitHub account before publishing.

## License

MIT. See [LICENSE](LICENSE).
