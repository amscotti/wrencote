#include <assert.h>
#include <string.h>

#include "http2/conn.h"
#include "http2/frame.h"

static size_t put_frame(uint8_t *out, uint8_t type, uint8_t flags, uint32_t stream_id,
                        const uint8_t *payload, size_t length) {
    out[0] = (uint8_t)(length >> 16);
    out[1] = (uint8_t)(length >> 8);
    out[2] = (uint8_t)length;
    out[3] = type;
    out[4] = flags;
    out[5] = (uint8_t)(stream_id >> 24);
    out[6] = (uint8_t)(stream_id >> 16);
    out[7] = (uint8_t)(stream_id >> 8);
    out[8] = (uint8_t)stream_id;
    if (length > 0) {
        memcpy(out + 9, payload, length);
    }
    return 9 + length;
}

int main(void) {
    wc_h2_conn *conn = wc_h2_conn_new(NULL);
    assert(conn != NULL);
    assert(!wc_h2_conn_closed(conn));
    size_t output_len = 0;
    assert(wc_h2_conn_output(conn, &output_len) != NULL);
    assert(output_len == 9);

    uint8_t input[128];
    memcpy(input, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
    size_t offset = 24;
    offset += put_frame(input + offset, WC_H2_SETTINGS, 0, 0, NULL, 0);
    const uint8_t ping[] = {'1', '2', '3', '4', '5', '6', '7', '8'};
    offset += put_frame(input + offset, WC_H2_PING, 0, 0, ping, sizeof ping);
    size_t consumed = 0;
    assert(wc_h2_conn_feed(conn, input, offset, &consumed) == WC_OK);
    assert(consumed == offset);
    assert(wc_h2_conn_output(conn, &output_len) != NULL);
    assert(output_len == 9 + 9 + 17);

    wc_h2_conn_output_consume(conn, output_len);
    assert(wc_h2_conn_output(conn, &output_len) != NULL);
    assert(output_len == 0);

    uint8_t ack[9];
    put_frame(ack, WC_H2_SETTINGS, 0x1, 0, NULL, 0);
    assert(wc_h2_conn_feed(conn, ack, sizeof ack, &consumed) == WC_OK);
    assert(consumed == sizeof ack);
    wc_h2_conn_output(conn, &output_len);
    assert(output_len == 0);

    uint8_t bad_preface[24] = {0};
    wc_h2_conn *bad = wc_h2_conn_new(NULL);
    assert(wc_h2_conn_feed(bad, bad_preface, sizeof bad_preface, &consumed) == WC_EPROTO);
    wc_h2_conn_free(bad);

    wc_h2_conn *partial = wc_h2_conn_new(NULL);
    assert(wc_h2_conn_feed(partial, input, 10, &consumed) == WC_EAGAIN);
    assert(consumed == 0);
    wc_h2_conn_free(partial);

    wc_h2_conn *oversized = wc_h2_conn_new(NULL);
    uint8_t large[9] = {0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(input, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
    memcpy(input + 24, large, sizeof large);
    assert(wc_h2_conn_feed(oversized, input, 24 + sizeof large, &consumed) == WC_EPROTO);
    wc_h2_conn_free(oversized);

    wc_h2_conn *window = wc_h2_conn_new(NULL);
    memcpy(input, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
    uint8_t zero_window[4] = {0, 0, 0, 0};
    offset = 24 + put_frame(input + 24, WC_H2_WINDOW_UPDATE, 0, 0, zero_window, 4);
    assert(wc_h2_conn_feed(window, input, offset, &consumed) == WC_EPROTO);
    wc_h2_conn_free(window);

    wc_h2_conn *goaway = wc_h2_conn_new(NULL);
    memcpy(input, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
    uint8_t goaway_payload[8] = {0};
    offset = 24 + put_frame(input + 24, WC_H2_GOAWAY, 0, 0, goaway_payload, 8);
    assert(wc_h2_conn_feed(goaway, input, offset, &consumed) == WC_OK);
    assert(wc_h2_conn_closed(goaway));
    wc_h2_conn_free(goaway);

    wc_h2_conn_output_consume(conn, 999);
    wc_h2_conn_free(conn);
    wc_h2_conn_free(NULL);
    wc_h2_conn_output_consume(NULL, 1);
    assert(!wc_h2_conn_closed(NULL));
    return 0;
}
