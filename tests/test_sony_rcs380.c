/* SPDX-License-Identifier: MIT */
#include "sony.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Synthetic USB transcripts from the Port-100 wire format. These are not
 * claimed to be hardware captures. Literal frames let the tests check the
 * protocol independently of the production frame encoder.
 */
#define ACK "0000ff00ff00"
#define SET_TYPE "0000ffffff0300fdd62a01ff00"
#define RF_OFF "0000ffffff0300fdd606002400"
#define SET_RF "0000ffffff0600fad60001010f011800"
#define SET_PROTOCOL \
    "0000ffffff2800d8d60200180101020103000400050006000708080009000a000b00" \
    "0c000e040f0010001100120013064b00"
#define POLL "0000ffffff0a00f6d6046e000600ffff0000b400"
#define OK_TYPE "0000ffffff0300fdd72b00fe00"
#define OK_OFF "0000ffffff0300fdd707002200"
#define OK_RF "0000ffffff0300fdd701002800"
#define OK_PROTOCOL "0000ffffff0300fdd703002600"
#define CARD \
    "0000ffffff1900e7d705000000000012010123456789abcdef00010203040506073500"
#define NO_CARD "0000ffffff0600fad70580000000a400"

typedef struct {
    bool write;
    const char *hex;
    int result;
} event;

typedef struct {
    const event *events;
    size_t count;
    size_t next;
} transcript;

#define WRITE(hex) {true, hex, NFC_OK}
#define READ(hex) {false, hex, NFC_OK}
#define READ_ERROR(code) {false, NULL, code}

static size_t decode(const char *hex, uint8_t *out, size_t cap)
{
    size_t count = strlen(hex) / 2;
    assert(strlen(hex) % 2 == 0);
    assert(count <= cap);
    for (size_t i = 0; i < count; ++i) {
        unsigned byte;
        assert(sscanf(hex + i * 2, "%2x", &byte) == 1);
        out[i] = (uint8_t)byte;
    }
    return count;
}

static const event *next_event(nfc_usb *usb, bool write, unsigned timeout)
{
    transcript *script = usb->opaque;
    assert(timeout > 0 && timeout <= 500);
    assert(script->next < script->count);
    const event *ev = script->events + script->next++;
    assert(ev->write == write);
    return ev;
}

static int mock_write(nfc_usb *usb, const uint8_t *bytes, size_t size,
                      unsigned timeout)
{
    uint8_t expected[128];
    const event *ev = next_event(usb, true, timeout);
    size_t expected_size = decode(ev->hex, expected, sizeof(expected));
    assert(expected_size == size);
    assert(memcmp(bytes, expected, size) == 0);
    return ev->result;
}

static int mock_read(nfc_usb *usb, uint8_t *bytes, size_t capacity,
                     unsigned timeout)
{
    const event *ev = next_event(usb, false, timeout);
    if (ev->result < 0) {
        nfc_error(usb->error, sizeof(usb->error), "synthetic USB error %d",
                  ev->result);
        return ev->result;
    }
    return (int)decode(ev->hex, bytes, capacity);
}

static void attach(nfc_usb *usb, transcript *script)
{
    memset(usb, 0, sizeof(*usb));
    usb->opaque = script;
    usb->vid = 0x054c;
    usb->pid = 0x06c3;
    usb->write_fn = mock_write;
    usb->read_fn = mock_read;
}

static void test_lifecycle(void)
{
    static const event events[] = {
        WRITE(ACK), READ_ERROR(NFC_TIMEOUT),
        WRITE(SET_TYPE), READ(ACK OK_TYPE),
        WRITE(RF_OFF), READ("0000"), READ("ff00ff00"), READ(OK_OFF),
        WRITE(SET_RF), READ(ACK OK_RF),
        WRITE(SET_PROTOCOL), READ(ACK OK_PROTOCOL),
        WRITE(POLL), READ(ACK NO_CARD),
        WRITE(POLL), READ(ACK "0000ffffff19"),
        READ("00e7d705000000000012010123456789abcdef00010203040506073500"),
        WRITE(RF_OFF), READ(ACK OK_OFF), WRITE(ACK)
    };
    const uint8_t expected[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    uint8_t idm[8];
    transcript script = {events, sizeof(events) / sizeof(events[0]), 0};
    nfc_usb usb;
    nfc_sony sony;

    attach(&usb, &script);
    assert(nfc_s380_init(&sony, &usb) == NFC_OK);
    assert(sony.initialized);
    assert(sony.kind == NFC_S380);
    assert(nfc_s380_poll(&sony, idm) == NFC_AGAIN);
    assert(nfc_s380_poll(&sony, idm) == NFC_OK);
    assert(memcmp(idm, expected, sizeof(idm)) == 0);

    /* Cleanup must still send RF-off after a signal, preserving diagnostics. */
    strcpy(usb.error, "original diagnostic");
    nfc_stopped = 1;
    nfc_s380_close(&sony);
    nfc_stopped = 0;
    assert(!sony.initialized);
    assert(strcmp(usb.error, "original diagnostic") == 0);
    assert(script.next == script.count);
}

static void test_poll_failure(const char *reply, int read_error,
                              int expected_result, const char *message)
{
    event events[] = {
        WRITE(POLL), READ(ACK), {false, reply, read_error}
    };
    transcript script = {events, sizeof(events) / sizeof(events[0]), 0};
    nfc_usb usb;
    nfc_sony sony;
    uint8_t idm[8];
    uint8_t untouched[8];

    attach(&usb, &script);
    memset(&sony, 0, sizeof(sony));
    sony.kind = NFC_S380;
    sony.initialized = true;
    nfc_frame_init(&sony.frame, &usb, NFC_FRAME_PORT100, false);
    memset(idm, 0xa5, sizeof(idm));
    memcpy(untouched, idm, sizeof(idm));
    assert(nfc_s380_poll(&sony, idm) == expected_result);
    assert(memcmp(idm, untouched, sizeof(idm)) == 0);
    if (message)
        assert(strstr(usb.error, message) != NULL);
    assert(script.next == script.count);
}

static void test_bad_initialization(void)
{
    static const event events[] = {
        WRITE(ACK), READ_ERROR(NFC_TIMEOUT),
        WRITE(SET_TYPE), READ(ACK "0000ffffff0200fed72bfe00")
    };
    transcript script = {events, sizeof(events) / sizeof(events[0]), 0};
    nfc_usb usb;
    nfc_sony sony;

    attach(&usb, &script);
    assert(nfc_s380_init(&sony, &usb) == NFC_ERR);
    assert(!sony.initialized);
    assert(strstr(usb.error, "invalid status length") != NULL);
    assert(script.next == script.count);
}

int main(void)
{
    test_lifecycle();
    test_poll_failure(NO_CARD, NFC_OK, NFC_AGAIN, NULL);
    test_poll_failure(NULL, NFC_TIMEOUT, NFC_TIMEOUT, "synthetic USB error");
    test_poll_failure(NULL, NFC_GONE, NFC_GONE, "synthetic USB error");
    /* Valid framing but a reported CRC failure must not become 'no card'. */
    test_poll_failure("0000ffffff0600fad705040000002000", NFC_OK, NFC_ERR,
                       "0x00000004");
    /* Correct USB frame, wrong FeliCa response code. */
    test_poll_failure(
        "0000ffffff1900e7d705000000000012020123456789abcdef00010203040506073400",
        NFC_OK, NFC_ERR, "invalid FeliCa Polling response");
    /* Status is present, but the RF packet is missing. */
    test_poll_failure("0000ffffff0700f9d70500000000002400", NFC_OK, NFC_ERR,
                       "invalid FeliCa Polling response");
    /* A response to another reader command must never supply an IDm. */
    test_poll_failure(
        "0000ffffff1900e7d707000000000012010123456789abcdef00010203040506073300",
        NFC_OK, NFC_ERR, "invalid response to command");
    /* The final checksum is intentionally corrupted. */
    test_poll_failure(
        "0000ffffff1900e7d705000000000012010123456789abcdef00010203040506073400",
        NFC_OK, NFC_ERR, "checksum");
    test_bad_initialization();
    puts("RC-S380 protocol tests passed");
    return 0;
}
