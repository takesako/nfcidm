/* SPDX-License-Identifier: MIT */
#include "sony.h"
#include "platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

enum { EXIT_FAILURE_IO = 1, EXIT_NO_CARD = 2, EXIT_NO_READER = 3,
       EXIT_BAD_CARD = 4, EXIT_USAGE = 64 };

static void usage(FILE *out)
{
    fputs("Usage: nfcidm [--reader N] [--timeout SECONDS] [--verbose]\n"
          "       nfcidm --list\n\n"
          "Read one FeliCa IDm and print 16 uppercase hexadecimal digits.\n\n"
          "  -l, --list          List supported readers without polling a card\n"
          "  -r, --reader N      Select the 1-based number shown by --list\n"
          "  -t, --timeout SEC   Wait up to SEC seconds (default 10; 0 = no limit)\n"
          "  -v, --verbose       Write diagnostics to stderr\n"
          "  -V, --version       Print the version\n"
          "  -h, --help          Show this help\n\n"
          "Readers: SONY RC-S320/330/360/370/380 (USB on macOS),\n"
          "         SONY RC-S320/330/360/370/380 (WinUSB on Windows),\n"
          "         SONY RC-S300 family and ACS ACR122U (PC/SC).\n",
          out);
}

static int number(const char *text, unsigned maximum, unsigned *value)
{
    char *end;
    if (!text || !*text)
        return -1;
    for (const char *p = text; *p; p++)
        if (*p < '0' || *p > '9')
            return -1;
    errno = 0;
    unsigned long n = strtoul(text, &end, 10);
    if (errno || *end || n > maximum)
        return -1;
    *value = (unsigned)n;
    return 0;
}

typedef struct {
    bool list, verbose, help, version;
    unsigned selected, timeout;
} cli_options;

/* A tiny portable option parser. It supports --option=value, -r2, -lv,
 * and traditional separated arguments without GNU getopt_long/unistd.
 */
static int parse_args(int argc, char **argv, cli_options *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->timeout = 10;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        char name = 0;
        if (!strcmp(arg, "--"))
            return i == argc - 1 ? 0 : -1;
        if (arg[0] != '-' || arg[1] == '\0')
            return -1;
        if (arg[1] == '-') {
            const char *equals = strchr(arg + 2, '=');
            size_t len = equals ? (size_t)(equals - (arg + 2)) : strlen(arg + 2);
            if (len == 4 && !strncmp(arg + 2, "list", len)) name = 'l';
            else if (len == 6 && !strncmp(arg + 2, "reader", len)) name = 'r';
            else if (len == 7 && !strncmp(arg + 2, "timeout", len)) name = 't';
            else if (len == 7 && !strncmp(arg + 2, "verbose", len)) name = 'v';
            else if (len == 7 && !strncmp(arg + 2, "version", len)) name = 'V';
            else if (len == 4 && !strncmp(arg + 2, "help", len)) name = 'h';
            else return -1;
            if (name == 'r' || name == 't') {
                value = equals ? equals + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!value) return -1;
            } else if (equals)
                return -1;
        } else {
            /* Short flags may be grouped; options taking a value end a group. */
            for (const char *p = arg + 1; *p; p++) {
                switch (*p) {
                case 'l': opt->list = true; break;
                case 'v': opt->verbose = true; break;
                case 'V': opt->version = true; break;
                case 'h': opt->help = true; break;
                case 'r': case 't':
                    value = p[1] ? p + 1 : (i + 1 < argc ? argv[++i] : NULL);
                    if (!value || number(value, *p == 'r' ? NFCIDM_MAX_READERS : 86400,
                                          *p == 'r' ? &opt->selected : &opt->timeout))
                        return -1;
                    if (*p == 'r' && opt->selected == 0)
                        return -1;
                    goto next_argument;
                default: return -1;
                }
            }
            continue;
        }
        switch (name) {
        case 'l': opt->list = true; break;
        case 'v': opt->verbose = true; break;
        case 'V': opt->version = true; break;
        case 'h': opt->help = true; break;
        case 'r': case 't':
            if (number(value, name == 'r' ? NFCIDM_MAX_READERS : 86400,
                       name == 'r' ? &opt->selected : &opt->timeout))
                return -1;
            if (name == 'r' && opt->selected == 0)
                return -1;
            break;
        default: return -1;
        }
next_argument:;
    }
    return opt->list && opt->selected ? -1 : 0;
}

static int compare_readers(const void *a, const void *b)
{
    const nfc_reader *x = a, *y = b;
    if (x->kind != y->kind)
        return x->kind < y->kind ? -1 : 1;
    if (x->location != y->location)
        return x->location < y->location ? -1 : 1;
    return strcmp(x->name, y->name);
}

static int read_one(const nfc_reader *reader, bool verbose, uint8_t idm[8],
                    char *error, size_t error_cap)
{
    int rc = NFC_ERR;
    if (reader->kind == NFC_PCSC) {
        nfc_pcsc *pcsc = NULL;
        rc = nfc_pcsc_open(reader, verbose, &pcsc, error, error_cap);
        if (rc != NFC_OK)
            return rc;
        while (!nfc_stopped) {
            rc = nfc_pcsc_poll(pcsc, idm, 200, error, error_cap);
            if (rc != NFC_AGAIN)
                break;
            nfc_sleep_ms(20);
        }
        nfc_pcsc_close(pcsc);
    } else {
        nfc_usb usb;
        nfc_sony sony;
        int (*init)(nfc_sony *, nfc_usb *) = NULL;
        int (*poll)(nfc_sony *, uint8_t[8]) = NULL;
        void (*close_reader)(nfc_sony *) = NULL;
        switch (reader->kind) {
        case NFC_S320: init = nfc_s320_init; poll = nfc_s320_poll; close_reader = nfc_s320_close; break;
        case NFC_S330: init = nfc_s330_init; poll = nfc_s330_poll; close_reader = nfc_s330_close; break;
        case NFC_S380: init = nfc_s380_init; poll = nfc_s380_poll; close_reader = nfc_s380_close; break;
        default: return NFC_ERR;
        }
        memset(&sony, 0, sizeof sony);
        rc = nfc_usb_open(reader, verbose, &usb);
        if (rc != NFC_OK) {
            nfc_error(error, error_cap, "%s", usb.error);
            return rc;
        }
        rc = init(&sony, &usb);
        if (rc == NFC_OK) {
            while (!nfc_stopped) {
                rc = poll(&sony, idm);
                if (rc != NFC_AGAIN)
                    break;
                nfc_sleep_ms(50);
            }
        }
        close_reader(&sony);
        nfc_error(error, error_cap, "%s", usb.error);
        nfc_usb_close(&usb);
    }
    return nfc_stopped ? NFC_CANCELLED : rc;
}

int main(int argc, char **argv)
{
    cli_options options;
    if (parse_args(argc, argv, &options)) {
        usage(stderr);
        return EXIT_USAGE;
    }
    if (options.version) {
        printf("nfcidm %s\n", NFCIDM_VERSION);
        return ferror(stdout) ? EXIT_FAILURE_IO : 0;
    }
    if (options.help) {
        usage(stdout);
        return ferror(stdout) ? EXIT_FAILURE_IO : 0;
    }
    if (nfc_platform_init() || nfc_platform_set_timeout(options.timeout)) {
        fputs("nfcidm: could not initialize interrupt/timeout handling\n", stderr);
        return EXIT_FAILURE_IO;
    }

    nfc_reader readers[NFCIDM_MAX_READERS];
    size_t count = 0;
    char usb_error[NFCIDM_ERROR_SIZE] = "", pcsc_error[NFCIDM_ERROR_SIZE] = "";
    int usb_rc = nfc_usb_enumerate(readers, NFCIDM_MAX_READERS, &count,
                                   usb_error, sizeof usb_error);
    int pcsc_rc = nfc_pcsc_enumerate(readers, NFCIDM_MAX_READERS, &count,
                                     pcsc_error, sizeof pcsc_error);
    if (nfc_stopped) {
        nfc_platform_clear_timeout();
        return nfc_platform_stopped_status();
    }
    if (options.verbose || count == 0 || options.list) {
        if (usb_error[0])
            fprintf(stderr, "nfcidm: %s\n", usb_error);
        if (pcsc_rc != NFC_OK)
            fprintf(stderr, "nfcidm: %s\n", pcsc_error);
    }
    qsort(readers, count, sizeof readers[0], compare_readers);
    if (options.list) {
        for (size_t i = 0; i < count; i++) {
            printf("%u\t%s\t", (unsigned)(i + 1), readers[i].kind == NFC_PCSC ? "PC/SC" : nfc_platform_usb_label());
            for (const unsigned char *p = (const unsigned char *)readers[i].name; *p; p++)
                putchar(*p < 0x20 || *p == 0x7f ? '?' : *p);
            if (readers[i].kind != NFC_PCSC)
                printf("\t%04x:%04x\t%s=%08x", readers[i].vid,
                       readers[i].pid,
#ifdef _WIN32
                       "instance",
#else
                       "location",
#endif
                       readers[i].location);
            putchar('\n');
        }
        nfc_platform_clear_timeout();
        /* A usable reader is sufficient even if another backend (e.g.
         * the optional PC/SC service) is unavailable. */
        return !ferror(stdout) &&
               (count > 0 || (usb_rc == NFC_OK && pcsc_rc == NFC_OK))
               ? 0 : EXIT_FAILURE_IO;
    }
    if (count == 0) {
        fputs("nfcidm: no supported reader found\n", stderr);
        nfc_platform_clear_timeout();
        return usb_rc == NFC_OK && pcsc_rc == NFC_OK ? EXIT_NO_READER : EXIT_FAILURE_IO;
    }
    if (options.selected == 0 && count > 1) {
        fputs("nfcidm: multiple readers found; use --list and --reader N\n", stderr);
        nfc_platform_clear_timeout();
        return EXIT_USAGE;
    }
    if (options.selected > count) {
        fputs("nfcidm: reader number is out of range; use --list\n", stderr);
        nfc_platform_clear_timeout();
        return EXIT_USAGE;
    }
    const nfc_reader *reader = &readers[options.selected ? options.selected - 1 : 0];
    if (options.verbose)
        fprintf(stderr, "nfcidm: using %s via %s\n", reader->name,
                reader->kind == NFC_PCSC ? "PC/SC" : nfc_platform_usb_label());
    uint8_t idm[8];
    char error[NFCIDM_ERROR_SIZE] = "";
    int rc = read_one(reader, options.verbose, idm, error, sizeof error);
    nfc_platform_clear_timeout();
    if (nfc_stopped)
        return nfc_platform_stopped_status();
    if (rc == NFC_OK) {
#ifdef _WIN32
        /* Preserve exactly 17 output bytes, even with Windows CRT text mode. */
        if (_setmode(_fileno(stdout), _O_BINARY) < 0) {
            fputs("nfcidm: could not set binary standard output\n", stderr);
            return EXIT_FAILURE_IO;
        }
#endif
        for (size_t i = 0; i < sizeof idm; i++)
            printf("%02X", (unsigned)idm[i]);
        putchar('\n');
        return ferror(stdout) ? EXIT_FAILURE_IO : 0;
    }
    fprintf(stderr, "nfcidm: %s\n", error[0] ? error : "reader communication failed");
    return rc == NFC_BAD_CARD ? EXIT_BAD_CARD : EXIT_FAILURE_IO;
}
