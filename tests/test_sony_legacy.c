/* SPDX-License-Identifier: MIT */
/* Synthetic protocol replays, not claims of physical-reader verification. */
#include "sony.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    nfc_kind kind;
    uint8_t input[256], reply[64], command[64];
    uint8_t command_log[16][16];
    size_t command_lengths[16];
    size_t input_size, input_at, reply_size, command_size, chunk, wire_limit;
    unsigned commands, controls, acks, fail_init_at;
    int read_status, write_status, ack_status;
    bool init_replies, corrupt_checksum, silent;
} mock;

static const uint8_t ack[] = {0, 0, 0xff, 0, 0xff, 0};
static const uint8_t idm_expected[] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t reply320[] = {
    0x5d, 17, 0x01, 1, 2, 3, 4, 5, 6, 7, 8,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};
static const uint8_t reply330[] = {
    0xd5, 0x4b, 1, 1, 18, 0x01, 1, 2, 3, 4, 5, 6, 7, 8,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

static void
queue_reply(mock *m, const uint8_t *p, size_t n)
{
    uint8_t sum = 0;
    m->input_at = 0;
    memcpy(m->input, ack, sizeof(ack));
    m->input_size = sizeof(ack);
    if (n == 0)
        return;
    assert(n <= sizeof(m->reply));
    uint8_t *frame = m->input + sizeof(ack);
    frame[0] = frame[1] = 0;
    frame[2] = 0xff;
    frame[3] = (uint8_t)n;
    frame[4] = (uint8_t)(0U - n);
    memcpy(frame + 5, p, n);
    for (size_t i = 0; i < n; ++i)
        sum = (uint8_t)(sum + p[i]);
    frame[n + 5] = (uint8_t)(0U - sum);
    if (m->corrupt_checksum)
        frame[n + 5] ^= 1;
    frame[n + 6] = 0;
    m->input_size += n + 7;
    if (m->wire_limit && m->input_size > m->wire_limit)
        m->input_size = m->wire_limit;
}

static int
mock_write(nfc_usb *usb, const uint8_t *data, size_t size, unsigned ms)
{
    mock *m = usb->opaque;
    assert(ms > 0);
    if (size == sizeof(ack) && memcmp(data, ack, size) == 0) {
        ++m->acks;
        return m->ack_status;
    }
    assert(size >= 7 && size == (size_t)data[3] + 7);
    assert(data[0] == 0 && data[1] == 0 && data[2] == 0xff);
    assert((uint8_t)(data[3] + data[4]) == 0);
    uint8_t sum = 0;
    for (size_t i = 5; i < size - 1; ++i)
        sum = (uint8_t)(sum + data[i]);
    assert(sum == 0 && data[size - 1] == 0);
    m->command_size = data[3];
    assert(m->command_size <= sizeof(m->command));
    memcpy(m->command, data + 5, m->command_size);
    assert(m->commands < sizeof(m->command_log) / sizeof(m->command_log[0]));
    assert(m->command_size <= sizeof(m->command_log[0]));
    memcpy(m->command_log[m->commands], m->command, m->command_size);
    m->command_lengths[m->commands] = m->command_size;
    ++m->commands;
    if (m->write_status != NFC_OK)
        return m->write_status;
    if (m->silent) {
        m->input_size = m->input_at = 0;
        return NFC_OK;
    }
    if (m->init_replies || (m->kind == NFC_S330 && m->command[1] != 0x4a)) {
        uint8_t response[2];
        if (m->fail_init_at == m->commands) {
            response[0] = 0x7f;
            queue_reply(m, response, 1);
        } else {
            response[0] = m->kind == NFC_S320 ?
                (uint8_t)(m->command[0] + 1) : 0xd5;
            response[1] = m->kind == NFC_S320 ? 0 :
                (uint8_t)(m->command[1] + 1);
            queue_reply(m, response, sizeof(response));
        }
    } else {
        queue_reply(m, m->reply, m->reply_size);
    }
    return NFC_OK;
}

static int
mock_control(nfc_usb *usb, uint8_t request, uint16_t value, uint16_t index,
              const uint8_t *data, size_t size, unsigned ms)
{
    mock *m = usb->opaque;
    assert(m->kind == NFC_S320);
    assert(request == 0 && value == 0 && index == 0);
    ++m->controls;
    return mock_write(usb, data, size, ms);
}

static int
mock_read(nfc_usb *usb, uint8_t *data, size_t size, unsigned ms)
{
    mock *m = usb->opaque;
    assert(ms > 0);
    size_t n = m->input_size - m->input_at;
    if (n == 0) {
        nfc_error(usb->error, sizeof(usb->error), "mock read result %d",
                  m->read_status);
        return m->read_status;
    }
    if (n > size)
        n = size;
    if (m->chunk && n > m->chunk)
        n = m->chunk;
    memcpy(data, m->input + m->input_at, n);
    m->input_at += n;
    return (int)n;
}

static void
start(nfc_sony *s, nfc_usb *usb, mock *m, nfc_kind kind)
{
    memset(m, 0, sizeof(*m));
    memset(usb, 0, sizeof(*usb));
    memset(s, 0, sizeof(*s));
    nfc_stopped = 0;
    m->kind = kind;
    m->read_status = NFC_TIMEOUT;
    usb->opaque = m;
    usb->write_fn = mock_write;
    usb->read_fn = mock_read;
    usb->control_fn = mock_control;
    s->kind = kind;
    s->initialized = true;
    nfc_frame_init(&s->frame, usb,
                   kind == NFC_S320 ? NFC_FRAME_NORMAL : NFC_FRAME_PN53X,
                   kind == NFC_S320);
}

static int
poll(nfc_sony *s, uint8_t idm[8])
{
    return s->kind == NFC_S320 ? nfc_s320_poll(s, idm) : nfc_s330_poll(s, idm);
}

static void
set_reply(mock *m, const uint8_t *p, size_t n)
{
    assert(n <= sizeof(m->reply));
    memcpy(m->reply, p, n);
    m->reply_size = n;
}

static void
test_polling(void)
{
    static const uint8_t command320[] = {0x5c, 6, 0, 0xff, 0xff, 0, 0};
    static const uint8_t command330[] = {0xd4, 0x4a, 1, 1, 0, 0xff, 0xff, 0, 0};
    static const uint8_t empty320[] = {0x5d, 0};
    static const uint8_t empty330[] = {0xd5, 0x4b, 0};
    for (int model = 0; model < 2; ++model) {
        nfc_kind kind = model ? NFC_S330 : NFC_S320;
        const uint8_t *reply = model ? reply330 : reply320;
        size_t reply_size = model ? sizeof(reply330) : sizeof(reply320);
        nfc_sony s;
        nfc_usb usb;
        mock m;
        uint8_t idm[8];

        /* A single combined ACK+data transfer and every small split. */
        for (size_t chunk = 0; chunk <= 8; ++chunk) {
            start(&s, &usb, &m, kind);
            m.chunk = chunk;
            set_reply(&m, reply, reply_size);
            assert(poll(&s, idm) == NFC_OK);
            assert(memcmp(idm, idm_expected, sizeof(idm)) == 0);
            const uint8_t *expected = model ? command330 : command320;
            size_t expected_size = model ? sizeof(command330) : sizeof(command320);
            assert(m.command_size == expected_size);
            assert(memcmp(m.command, expected, expected_size) == 0);
            assert(m.controls == (model ? 0U : 1U));
        }

        /* Truncation must never produce a shortened or fabricated IDm. */
        for (size_t n = 1; n < reply_size; ++n) {
            start(&s, &usb, &m, kind);
            set_reply(&m, reply, n);
            memset(idm, 0xa5, sizeof(idm));
            assert(poll(&s, idm) == NFC_ERR);
            for (size_t i = 0; i < sizeof(idm); ++i)
                assert(idm[i] == 0xa5);
        }
        start(&s, &usb, &m, kind);
        set_reply(&m, model ? empty330 : empty320,
                   model ? sizeof(empty330) : sizeof(empty320));
        assert(poll(&s, idm) == NFC_AGAIN);
        assert(m.acks == 0);
        assert(s.frame.used == 0);
        set_reply(&m, reply, reply_size);
        assert(poll(&s, idm) == NFC_OK);
        assert(memcmp(idm, idm_expected, sizeof(idm)) == 0);

        start(&s, &usb, &m, kind);
        set_reply(&m, reply, reply_size);
        m.corrupt_checksum = true;
        assert(poll(&s, idm) == NFC_ERR);

        start(&s, &usb, &m, kind);
        set_reply(&m, reply, reply_size);
        m.reply[model ? 4 : 1] = 255;
        assert(poll(&s, idm) == NFC_ERR);

        start(&s, &usb, &m, kind);
        assert(poll(&s, idm) == NFC_AGAIN); /* ACK, then RF timeout */
        assert(m.acks == (model ? 2U : 0U));
        assert(s.frame.used == 0);
        if (model) {
            static const uint8_t reset[] = {0xd4, 0x18, 1};
            static const uint8_t rf_off[] = {0xd4, 0x32, 1, 0};
            static const uint8_t rf_on[] = {0xd4, 0x32, 1, 1};
            assert(m.commands == 4);
            assert(m.command_lengths[1] == sizeof(reset));
            assert(memcmp(m.command_log[1], reset, sizeof(reset)) == 0);
            assert(m.command_lengths[2] == sizeof(rf_off));
            assert(memcmp(m.command_log[2], rf_off, sizeof(rf_off)) == 0);
            assert(m.command_lengths[3] == sizeof(rf_on));
            assert(memcmp(m.command_log[3], rf_on, sizeof(rf_on)) == 0);
        }
        set_reply(&m, reply, reply_size);
        assert(poll(&s, idm) == NFC_OK);
        assert(memcmp(idm, idm_expected, sizeof(idm)) == 0);

        /* Partial USB responses are communication failures, not no card. */
        start(&s, &usb, &m, kind);
        set_reply(&m, reply, reply_size);
        m.wire_limit = sizeof(ack) + 9;
        assert(poll(&s, idm) == NFC_TIMEOUT);
        assert(s.frame.command_acked && s.frame.used != 0);
        assert(m.commands == 1 && m.acks == 0);

        start(&s, &usb, &m, kind);
        m.silent = true;
        assert(poll(&s, idm) == NFC_TIMEOUT);
        assert(!s.frame.command_acked && s.frame.used == 0);
        assert(m.commands == 1 && m.acks == 0);

        start(&s, &usb, &m, kind);
        set_reply(&m, reply, reply_size);
        m.wire_limit = 3; /* A partial ACK is also a USB failure. */
        assert(poll(&s, idm) == NFC_TIMEOUT);
        assert(!s.frame.command_acked && s.frame.used == 3);
        assert(m.commands == 1 && m.acks == 0);

        /* An ACK from a preceding command cannot mask a send timeout. */
        start(&s, &usb, &m, kind);
        set_reply(&m, reply, reply_size);
        assert(poll(&s, idm) == NFC_OK);
        assert(s.frame.command_acked);
        m.write_status = NFC_TIMEOUT;
        assert(poll(&s, idm) == NFC_TIMEOUT);
        assert(!s.frame.command_acked);
        assert(m.acks == 0);

        if (model) {
            start(&s, &usb, &m, kind);
            m.ack_status = NFC_GONE;
            assert(poll(&s, idm) == NFC_GONE);
            /* Failed resynchronization must not become a no-card retry. */
            for (unsigned fail = 2; fail <= 4; ++fail) {
                start(&s, &usb, &m, kind);
                m.fail_init_at = fail;
                assert(poll(&s, idm) == NFC_ERR);
                assert(m.commands == fail);
            }
        }

        start(&s, &usb, &m, kind);
        m.read_status = NFC_GONE;
        assert(poll(&s, idm) == NFC_GONE);
        assert(m.acks == 0);

        start(&s, &usb, &m, kind);
        m.write_status = NFC_ERR;
        assert(poll(&s, idm) == NFC_ERR);

        start(&s, &usb, &m, kind);
        nfc_stopped = 1;
        assert(poll(&s, idm) == NFC_CANCELLED);
        assert(m.commands == 0);
        nfc_stopped = 0;
    }
}

static void
test_initialization_and_cleanup(void)
{
    for (int model = 0; model < 2; ++model) {
        nfc_kind kind = model ? NFC_S330 : NFC_S320;
        nfc_sony s;
        nfc_usb usb;
        mock m;
        start(&s, &usb, &m, kind);
        m.init_replies = true;
        int rc = model ? nfc_s330_init(&s, &usb) : nfc_s320_init(&s, &usb);
        assert(rc == NFC_OK && s.initialized);
        assert(m.commands == (model ? 4U : 7U));
        assert(m.acks == (model ? 2U : 0U));
        (void)snprintf(usb.error, sizeof(usb.error), "preserve original failure");
        m.write_status = NFC_GONE;
        nfc_stopped = 1; /* Cleanup still attempts to stop the RF field. */
        if (model)
            nfc_s330_close(&s);
        else
            nfc_s320_close(&s);
        nfc_stopped = 0;
        assert(!s.initialized);
        assert(m.commands > (model ? 4U : 7U));
        assert(strcmp(usb.error, "preserve original failure") == 0);

        /* Rejection of any setup operation must stop initialization. */
        for (unsigned fail = 1; fail <= (model ? 4U : 7U); ++fail) {
            start(&s, &usb, &m, kind);
            m.init_replies = true;
            m.fail_init_at = fail;
            rc = model ? nfc_s330_init(&s, &usb) : nfc_s320_init(&s, &usb);
            assert(rc == NFC_ERR && !s.initialized);
            assert(m.commands == fail);
        }
    }
}

int
main(void)
{
    test_polling();
    test_initialization_and_cleanup();
    puts("sony legacy protocol replay tests passed (no hardware)");
    return 0;
}
