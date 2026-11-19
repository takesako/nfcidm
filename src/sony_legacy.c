/* SPDX-License-Identifier: MIT */
/*
 * IDm-only RC-S320 and RC-S330/RC-S360/RC-S370 (RC-S956) implementation.
 *
 * This is original code. The wire commands and initialization register
 * values were checked against the public libpafe, nfcpy and webpasori
 * implementations; their implementation code is not included here.
 */
#include "sony.h"

#include <string.h>

enum {
    INIT_TIMEOUT_MS = 1000,
    POLL_TIMEOUT_MS = 500,
    CLEANUP_TIMEOUT_MS = 100,
    RESPONSE_CAPACITY = 64
};

static int
protocol_error(nfc_sony *s, const char *message)
{
    nfc_error(s->frame.usb->error, sizeof(s->frame.usb->error),
              "%s: %s", s->kind == NFC_S320 ? "RC-S320" : "RC-S330",
              message);
    return NFC_ERR;
}

static void
prepare(nfc_sony *s, nfc_usb *usb, nfc_kind kind)
{
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    nfc_frame_init(&s->frame, usb,
                   kind == NFC_S320 ? NFC_FRAME_NORMAL : NFC_FRAME_PN53X,
                   kind == NFC_S320);
}

static int
ready(nfc_sony *s, nfc_kind kind)
{
    if (s == NULL || s->frame.usb == NULL)
        return NFC_ERR;
    if (s->kind != kind || !s->initialized)
        return protocol_error(s, "reader is not initialized");
    return nfc_stopped ? NFC_CANCELLED : NFC_OK;
}

/* One host command and its fully framed reply. Keep USB failures distinct
 * from malformed protocol replies, so a removed reader is not a missing card. */
static int
s320_command(nfc_sony *s, const uint8_t *command, size_t command_size,
             uint8_t *response, size_t *response_size, unsigned timeout_ms)
{
    int result = nfc_frame_command(&s->frame, command, command_size,
                                    response, response_size, timeout_ms);
    if (result != NFC_OK)
        return result;
    if (*response_size == 0 || response[0] != (uint8_t)(command[0] + 1))
        return protocol_error(s, "unexpected command response");
    return NFC_OK;
}

static int
s330_command(nfc_sony *s, const uint8_t *command, size_t command_size,
             uint8_t *response, size_t *response_size, unsigned timeout_ms)
{
    int result = nfc_frame_command(&s->frame, command, command_size,
                                    response, response_size, timeout_ms);
    if (result != NFC_OK)
        return result;
    if (*response_size < 2 || response[0] != 0xd5 ||
        response[1] != (uint8_t)(command[1] + 1))
        return protocol_error(s, "unexpected command response");
    return NFC_OK;
}

int
nfc_s320_init(nfc_sony *s, nfc_usb *usb)
{
    /* The old reader needs its analogue front-end registers initialized
     * before enabling its RF field. 0x62 reads/writes reader registers. */
    static const struct {
        uint8_t size;
        uint8_t bytes[6];
    } setup[] = {
        {3, {0x62, 0x01, 0x82}},
        {4, {0x62, 0x02, 0x80, 0x81}},
        {6, {0x62, 0x22, 0x80, 0xcc, 0x81, 0x88}},
        {4, {0x62, 0x02, 0x80, 0x81}},
        {4, {0x62, 0x02, 0x82, 0x87}},
        {4, {0x62, 0x21, 0x25, 0x58}},
        {2, {0x5a, 0x80}}
    };
    uint8_t response[RESPONSE_CAPACITY];

    if (s == NULL || usb == NULL)
        return NFC_ERR;
    prepare(s, usb, NFC_S320);
    nfc_frame_drain(&s->frame);
    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        size_t size = sizeof(response);
        int result;
        if (nfc_stopped)
            return NFC_CANCELLED;
        result = s320_command(s, setup[i].bytes, setup[i].size,
                              response, &size, INIT_TIMEOUT_MS);
        if (result != NFC_OK)
            return result;
        if (setup[i].bytes[0] == 0x62 && (size < 2 || response[1] != 0))
            return protocol_error(s, "reader register initialization failed");
    }
    nfc_sleep_ms(10);
    s->initialized = true;
    return NFC_OK;
}

int
nfc_s320_poll(nfc_sony *s, uint8_t idm[8])
{
    /* 5C: transmit FeliCa frame; 06 includes the over-the-air length
     * octet. Polling: all systems (FFFF), no optional data, one slot. */
    static const uint8_t polling[] = {
        0x5c, 0x06, 0x00, 0xff, 0xff, 0x00, 0x00
    };
    uint8_t response[RESPONSE_CAPACITY];
    size_t size = sizeof(response), card_size;
    int result = ready(s, NFC_S320);

    if (result != NFC_OK || idm == NULL)
        return result != NFC_OK ? result : NFC_ERR;
    result = s320_command(s, polling, sizeof(polling), response, &size,
                          POLL_TIMEOUT_MS);
    if (result == NFC_TIMEOUT) {
        /* A failed transmission, missing ACK or incomplete data frame is
         * a USB failure, not evidence that the card is absent. */
        if (!s->frame.command_acked || s->frame.used != 0)
            return result;
        nfc_frame_drain(&s->frame);
        return nfc_stopped ? NFC_CANCELLED : NFC_AGAIN;
    }
    if (result != NFC_OK)
        return result;
    if (size == 2 && response[1] == 0)
        return NFC_AGAIN;
    if (size < 2)
        return protocol_error(s, "truncated polling response");

    card_size = size - 2;
    /* The 5D envelope is removed here, including its length octet.
     * Accept either length convention seen in published implementations:
     * the card payload size, or that size plus the FeliCa length octet.
     * Both conventions still require a complete 01 + IDm + PMm response. */
    if ((card_size != 17 && card_size != 19) || response[2] != 0x01 ||
        (response[1] != card_size && response[1] != card_size + 1))
        return protocol_error(s, "invalid FeliCa polling response");
    memcpy(idm, response + 3, 8);
    return NFC_OK;
}

void
nfc_s320_close(nfc_sony *s)
{
    static const uint8_t reset[] = {0x54};
    uint8_t response[RESPONSE_CAPACITY];
    size_t size = sizeof(response);
    char saved_error[NFCIDM_ERROR_SIZE];

    if (s == NULL || s->frame.usb == NULL || s->kind != NFC_S320)
        return;
    memcpy(saved_error, s->frame.usb->error, sizeof(saved_error));
    /* This is also safe after a partial initialization. */
    (void)s320_command(s, reset, sizeof(reset), response, &size,
                       CLEANUP_TIMEOUT_MS);
    s->initialized = false;
    memcpy(s->frame.usb->error, saved_error, sizeof(saved_error));
}

/* RC-S956 ResetMode restores its strict command state machine to mode 0.
 * The host ACK after the reset response and its settling delay are needed
 * before the next command. This is not a USB device reset. */
static int
s330_reset(nfc_sony *s, unsigned timeout_ms)
{
    static const uint8_t reset[] = {0xd4, 0x18, 0x01};
    uint8_t response[RESPONSE_CAPACITY];
    size_t size = sizeof(response);
    int result = s330_command(s, reset, sizeof(reset), response, &size,
                              timeout_ms);
    if (result == NFC_OK)
        result = nfc_frame_ack(&s->frame, timeout_ms);
    if (result == NFC_OK)
        nfc_sleep_ms(10);
    return result;
}

/* Recover only an acknowledged polling command that produced no data.
 * The RC-S956 command state and RF field are restored before retrying;
 * these short operations do not repeat the full initialization sequence. */
static int
s330_recover_polling(nfc_sony *s)
{
    static const uint8_t field_commands[][4] = {
        {0xd4, 0x32, 0x01, 0x00},
        {0xd4, 0x32, 0x01, 0x01}
    };
    uint8_t response[RESPONSE_CAPACITY];
    int result = nfc_frame_ack(&s->frame, CLEANUP_TIMEOUT_MS);

    if (result != NFC_OK)
        return result;
    nfc_frame_drain(&s->frame);
    if (nfc_stopped)
        return NFC_CANCELLED;
    result = s330_reset(s, CLEANUP_TIMEOUT_MS);
    if (result != NFC_OK)
        return result;
    for (size_t i = 0; i < sizeof(field_commands) / sizeof(field_commands[0]); ++i) {
        size_t size = sizeof(response);
        if (nfc_stopped)
            return NFC_CANCELLED;
        result = s330_command(s, field_commands[i], sizeof(field_commands[i]),
                              response, &size, CLEANUP_TIMEOUT_MS);
        if (result != NFC_OK)
            return result;
    }
    nfc_sleep_ms(10);
    return nfc_stopped ? NFC_CANCELLED : NFC_OK;
}

int
nfc_s330_init(nfc_sony *s, nfc_usb *usb)
{
    static const struct {
        uint8_t size;
        uint8_t bytes[6];
    } setup[] = {
        {4, {0xd4, 0x32, 0x01, 0x00}}, /* RF off */
        {6, {0xd4, 0x32, 0x05, 0x00, 0x00, 0x01}}, /* bounded retries */
        {4, {0xd4, 0x32, 0x01, 0x01}}  /* RF on */
    };
    uint8_t response[RESPONSE_CAPACITY];
    int result;

    if (s == NULL || usb == NULL)
        return NFC_ERR;
    prepare(s, usb, NFC_S330);
    if (nfc_stopped)
        return NFC_CANCELLED;
    /* Abort a command left running by a previous interrupted process. */
    result = nfc_frame_ack(&s->frame, INIT_TIMEOUT_MS);
    if (result != NFC_OK)
        return result;
    nfc_frame_drain(&s->frame);
    result = s330_reset(s, INIT_TIMEOUT_MS);
    if (result != NFC_OK)
        return result;
    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        size_t size = sizeof(response);
        if (nfc_stopped)
            return NFC_CANCELLED;
        result = s330_command(s, setup[i].bytes, setup[i].size,
                              response, &size, INIT_TIMEOUT_MS);
        if (result != NFC_OK)
            return result;
    }
    /* Allow a newly powered FeliCa card to become ready for polling. */
    nfc_sleep_ms(10);
    s->initialized = true;
    return NFC_OK;
}

int
nfc_s330_poll(nfc_sony *s, uint8_t idm[8])
{
    /* InListPassiveTarget, MaxTg=1, BrTy=212F, FeliCa polling. */
    static const uint8_t polling[] = {
        0xd4, 0x4a, 0x01, 0x01, 0x00, 0xff, 0xff, 0x00, 0x00
    };
    uint8_t response[RESPONSE_CAPACITY];
    size_t size = sizeof(response);
    int result = ready(s, NFC_S330);

    if (result != NFC_OK || idm == NULL)
        return result != NFC_OK ? result : NFC_ERR;
    result = s330_command(s, polling, sizeof(polling), response, &size,
                          POLL_TIMEOUT_MS);
    if (result == NFC_TIMEOUT) {
        if (!s->frame.command_acked || s->frame.used != 0)
            return result;
        result = s330_recover_polling(s);
        return result == NFC_OK ? NFC_AGAIN : result;
    }
    if (result != NFC_OK)
        return result;
    if (size == 3 && response[2] == 0)
        return NFC_AGAIN;
    /* D5 4B NbTg Tg Len 01 IDm[8] PMm[8] [system-code[2]]. Len includes
     * its own octet. Check every length before reading or copying IDm. */
    if (size < 22 || response[2] != 1 || response[3] != 1 ||
        (response[4] != 18 && response[4] != 20) ||
        size != (size_t)response[4] + 4 || response[5] != 0x01)
        return protocol_error(s, "invalid FeliCa polling response");
    memcpy(idm, response + 6, 8);
    return NFC_OK;
}

void
nfc_s330_close(nfc_sony *s)
{
    static const uint8_t rf_off[] = {0xd4, 0x32, 0x01, 0x00};
    uint8_t response[RESPONSE_CAPACITY];
    size_t size = sizeof(response);
    char saved_error[NFCIDM_ERROR_SIZE];

    if (s == NULL || s->frame.usb == NULL || s->kind != NFC_S330)
        return;
    memcpy(saved_error, s->frame.usb->error, sizeof(saved_error));
    (void)nfc_frame_ack(&s->frame, CLEANUP_TIMEOUT_MS);
    nfc_frame_drain(&s->frame);
    (void)s330_reset(s, CLEANUP_TIMEOUT_MS);
    (void)s330_command(s, rf_off, sizeof(rf_off), response, &size,
                       CLEANUP_TIMEOUT_MS);
    s->initialized = false;
    memcpy(s->frame.usb->error, saved_error, sizeof(saved_error));
}
