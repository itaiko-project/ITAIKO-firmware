#include "usb/device/vendor/bpreader_serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BPREADER_ACCESS_CODE_HEX
#define BPREADER_ACCESS_CODE_HEX "00000000000000000000"
#endif

#ifndef BPREADER_CARD_PRESENT_DEFAULT
#define BPREADER_CARD_PRESENT_DEFAULT 0
#endif

enum {
    BPREADER_FRAME_MIN = 9,
    BPREADER_FRAME_MAX = 128,
    BPREADER_CARD_BYTES = 10,
    BPREADER_MIFARE_BLOCK_SIZE = 16,
    BPREADER_MIFARE_BLOCK_COUNT = 64,
    BPREADER_FELICA_BLOCK_SIZE = 16,
};

enum {
    MIFARE_CMD_AUTH_KEY_A = 0x60,
    MIFARE_CMD_AUTH_KEY_B = 0x61,
    MIFARE_CMD_READ = 0x30,
};

typedef struct {
    uint8_t block[BPREADER_MIFARE_BLOCK_SIZE];
} mifare_block_t;

typedef struct {
    bool card_present;
    uint8_t access_code[BPREADER_CARD_BYTES];
    mifare_block_t blocks[BPREADER_MIFARE_BLOCK_COUNT];
} bpreader_state_t;

static bpreader_state_t bpreader;

static const uint8_t FELICA_IDM[8] = {0x01, 0x2E, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
static const uint8_t FELICA_PMM[8] = {0x00, 0xF1, 0x00, 0x00, 0x00, 0x01, 0x43, 0x00};
static const uint8_t FELICA_SYSTEM_CODE[2] = {0x88, 0xB4};

typedef struct {
    uint16_t id;
    uint8_t data[BPREADER_FELICA_BLOCK_SIZE];
} felica_block_t;

static const felica_block_t FELICA_BLOCKS[] = {
    {0x8082, {0x01, 0x2E, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0x8000, {0xCE, 0xD6, 0x6F, 0x8F, 0x43, 0xD7, 0x25, 0xAD, 0x9F, 0xA7, 0xD9, 0x6C, 0x44, 0xB5, 0x1F, 0x3D}},
};

#define SOL_DECK_SIZE 22
#define SOL_JOKER_A 21
#define SOL_JOKER_B 22

typedef struct {
    char cards[SOL_DECK_SIZE];
} sol_deck_t;

static const sol_deck_t SOL_INIT_DECK = {
    .cards = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22},
};

static char sol_char_to_num(char c) { return (char)(c - '0' + 1); }

static char sol_num_to_char(char num) {
    while (num < 1) {
        num = (char)(num + 10);
    }
    return (char)((num - 1) % 10 + '0');
}

static void sol_move_card(sol_deck_t *deck, char card) {
    int p = 0;
    for (int i = 0; i < SOL_DECK_SIZE; i++) {
        if (deck->cards[i] == card) {
            p = i;
            break;
        }
    }

    if (p < SOL_DECK_SIZE - 1) {
        deck->cards[p] = deck->cards[p + 1];
        deck->cards[p + 1] = card;
        return;
    }

    for (int i = SOL_DECK_SIZE - 1; i > 1; i--) {
        deck->cards[i] = deck->cards[i - 1];
    }
    deck->cards[1] = card;
}

static void sol_cut_deck(sol_deck_t *deck, char point) {
    sol_deck_t tmp;
    memcpy(tmp.cards, &deck->cards[(size_t)point], SOL_DECK_SIZE - point - 1);
    memcpy(&tmp.cards[SOL_DECK_SIZE - point - 1], deck->cards, point);
    memcpy(deck->cards, tmp.cards, SOL_DECK_SIZE - 1);
}

static void sol_swap_outside_joker(sol_deck_t *deck) {
    int j1 = -1;
    int j2 = -1;
    sol_deck_t tmp;

    for (int i = 0; i < SOL_DECK_SIZE; i++) {
        if (deck->cards[i] == SOL_JOKER_A || deck->cards[i] == SOL_JOKER_B) {
            if (j1 == -1) {
                j1 = i;
            } else {
                j2 = i;
            }
        }
    }

    if (0 < SOL_DECK_SIZE - j2 - 1) {
        memcpy(tmp.cards, &deck->cards[j2 + 1], SOL_DECK_SIZE - j2 - 1);
    }
    tmp.cards[SOL_DECK_SIZE - j2 - 1] = deck->cards[j1];
    if (0 < j2 - j1 - 1) {
        memcpy(&tmp.cards[SOL_DECK_SIZE - j2], &deck->cards[j1 + 1], j2 - j1 - 1);
    }
    tmp.cards[SOL_DECK_SIZE - j1 - 1] = deck->cards[j2];
    if (0 < j1) {
        memcpy(&tmp.cards[SOL_DECK_SIZE - j1], deck->cards, j1);
    }
    memcpy(deck->cards, tmp.cards, SOL_DECK_SIZE);
}

static void sol_cut_by_bottom_card(sol_deck_t *deck) {
    char p = deck->cards[SOL_DECK_SIZE - 1];
    if (p == SOL_JOKER_B) {
        p = SOL_JOKER_A;
    }
    sol_cut_deck(deck, p);
}

static char sol_get_top_card_num(sol_deck_t *deck) {
    char p = deck->cards[0];
    if (p == SOL_JOKER_B) {
        p = SOL_JOKER_A;
    }
    return deck->cards[(size_t)p];
}

static void sol_deck_hash(sol_deck_t *deck) {
    char p;
    do {
        sol_move_card(deck, SOL_JOKER_A);
        sol_move_card(deck, SOL_JOKER_B);
        sol_move_card(deck, SOL_JOKER_B);
        sol_swap_outside_joker(deck);
        sol_cut_by_bottom_card(deck);
        p = sol_get_top_card_num(deck);
    } while (p == SOL_JOKER_A || p == SOL_JOKER_B);
}

static void sol_create_deck(sol_deck_t *deck, const char *key) {
    memcpy(deck, &SOL_INIT_DECK, sizeof(*deck));
    for (int p = 0; key[p] != '\0'; p++) {
        sol_deck_hash(deck);
        sol_cut_deck(deck, sol_char_to_num(key[p]));
    }
}

static void sol_cipher_decode(const char *key, const char *src, char *dst) {
    sol_deck_t deck;
    sol_create_deck(&deck, key);

    int i = 0;
    while (src[i] != '\0') {
        sol_deck_hash(&deck);
        dst[i] = sol_num_to_char((char)(sol_char_to_num(src[i]) - sol_get_top_card_num(&deck)));
        i++;
    }
    dst[i] = '\0';
}

static uint8_t hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (uint8_t)(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return (uint8_t)(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f') {
        return (uint8_t)(c - 'a' + 10);
    }
    return 0;
}

static bool parse_access_code(const char access_code[21], uint8_t out[BPREADER_CARD_BYTES]) {
    if (!access_code) {
        return false;
    }

    for (size_t i = 0; i < BPREADER_CARD_BYTES; i++) {
        const char hi = access_code[i * 2];
        const char lo = access_code[i * 2 + 1];
        if (!hi || !lo) {
            return false;
        }
        out[i] = (uint8_t)((hex_nibble(hi) << 4) | hex_nibble(lo));
        if ((out[i] & 0xF0) > 0x90 || (out[i] & 0x0F) > 0x09) {
            return false;
        }
    }

    return access_code[20] == '\0';
}

static void populate_card(void) {
    memset(bpreader.blocks, 0, sizeof(bpreader.blocks));

    memcpy(&bpreader.blocks[0].block[0], bpreader.access_code, 4);
    memcpy(&bpreader.blocks[2].block[6], bpreader.access_code, BPREADER_CARD_BYTES);

    char access_code[21];
    snprintf(access_code, sizeof(access_code), "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             bpreader.access_code[0], bpreader.access_code[1], bpreader.access_code[2], bpreader.access_code[3],
             bpreader.access_code[4], bpreader.access_code[5], bpreader.access_code[6], bpreader.access_code[7],
             bpreader.access_code[8], bpreader.access_code[9]);

    char hashed_id[9];
    char serial_text[9];
    memcpy(hashed_id, &access_code[5], 8);
    hashed_id[8] = '\0';
    sol_cipher_decode(&access_code[13], hashed_id, serial_text);

    const uint32_t serial = (uint32_t)strtoul(serial_text, NULL, 10);
    bpreader.blocks[1].block[12] = (uint8_t)(serial >> 24);
    bpreader.blocks[1].block[13] = (uint8_t)(serial >> 16);
    bpreader.blocks[1].block[14] = (uint8_t)(serial >> 8);
    bpreader.blocks[1].block[15] = (uint8_t)serial;
}

static size_t build_response(uint8_t response_cmd, const uint8_t *data, size_t data_len, uint8_t *out, size_t out_cap) {
    const size_t frame_len = data_len + 9;
    if (out_cap < frame_len || data_len > 0xFD) {
        return 0;
    }

    const uint8_t len = (uint8_t)(data_len + 2);
    out[0] = 0x00;
    out[1] = 0x00;
    out[2] = 0xFF;
    out[3] = len;
    out[4] = (uint8_t)(0x100u - len);
    out[5] = 0xD5;
    out[6] = response_cmd;

    if (data_len && data) {
        memcpy(&out[7], data, data_len);
    }

    uint8_t sum = 0;
    for (size_t i = 0; i < data_len + 7; i++) {
        sum = (uint8_t)(sum + out[i]);
    }
    out[data_len + 7] = (uint8_t)(0xFFu - sum);
    out[data_len + 8] = 0x00;

    return frame_len;
}

static size_t handle_poll(uint8_t *tx, size_t tx_cap) {
    if (!bpreader.card_present) {
        const uint8_t empty[3] = {0x00, 0x00, 0x00};
        return build_response(0x4B, empty, sizeof(empty), tx, tx_cap);
    }

    uint8_t data[22] = {0x01, 0x01, 0x14, 0x01};
    memcpy(&data[4], FELICA_IDM, sizeof(FELICA_IDM));
    memcpy(&data[12], FELICA_PMM, sizeof(FELICA_PMM));
    memcpy(&data[20], FELICA_SYSTEM_CODE, sizeof(FELICA_SYSTEM_CODE));
    return build_response(0x4B, data, sizeof(data), tx, tx_cap);
}

static const uint8_t *felica_find_block(uint16_t block_id) {
    for (size_t i = 0; i < sizeof(FELICA_BLOCKS) / sizeof(FELICA_BLOCKS[0]); i++) {
        if (FELICA_BLOCKS[i].id == block_id) {
            return FELICA_BLOCKS[i].data;
        }
    }
    return NULL;
}

static bool felica_decode_block_id(const uint8_t *desc, size_t desc_len, size_t *desc_pos, uint16_t *block_id) {
    if (!desc || !desc_pos || !block_id || *desc_pos >= desc_len) {
        return false;
    }

    const uint8_t first = desc[(*desc_pos)++];
    if (first & 0x80) {
        if (*desc_pos >= desc_len) {
            return false;
        }
        *block_id = (uint16_t)(((uint16_t)first << 8) | desc[(*desc_pos)++]);
        return true;
    }

    if (*desc_pos + 1 >= desc_len) {
        return false;
    }
    *desc_pos += 1;
    *block_id = desc[*desc_pos];
    *desc_pos += 1;
    *block_id |= (uint16_t)desc[(*desc_pos)++] << 8;
    return true;
}

static size_t handle_felica_read_without_encryption(const uint8_t *felica, size_t felica_len, uint8_t *tx,
                                                    size_t tx_cap) {
    if (!bpreader.card_present || felica_len < 14 || memcmp(&felica[2], FELICA_IDM, sizeof(FELICA_IDM)) != 0) {
        const uint8_t data[1] = {0x01};
        return build_response(0xA1, data, sizeof(data), tx, tx_cap);
    }

    size_t pos = 10;
    const uint8_t service_count = felica[pos++];
    pos += (size_t)service_count * 2;
    if (pos >= felica_len) {
        return 0;
    }

    const uint8_t block_count = felica[pos++];
    const size_t data_len = (size_t)3 + sizeof(FELICA_IDM) + 3 + (size_t)block_count * BPREADER_FELICA_BLOCK_SIZE;
    if (data_len + 9 > tx_cap || data_len > 0xFD) {
        return 0;
    }

    uint8_t data[96] = {0};
    if (data_len > sizeof(data)) {
        return 0;
    }

    size_t out = 0;
    data[out++] = 0x00;
    data[out++] = (uint8_t)(13 + block_count * BPREADER_FELICA_BLOCK_SIZE);
    data[out++] = 0x07;
    memcpy(&data[out], FELICA_IDM, sizeof(FELICA_IDM));
    out += sizeof(FELICA_IDM);
    data[out++] = 0x00;
    data[out++] = 0x00;
    data[out++] = block_count;

    for (uint8_t i = 0; i < block_count; i++) {
        uint16_t block_id = 0;
        if (!felica_decode_block_id(felica, felica_len, &pos, &block_id)) {
            return 0;
        }

        const uint8_t *block = felica_find_block(block_id);
        if (block) {
            memcpy(&data[out], block, BPREADER_FELICA_BLOCK_SIZE);
        }
        out += BPREADER_FELICA_BLOCK_SIZE;
    }

    return build_response(0xA1, data, out, tx, tx_cap);
}

static size_t handle_felica_write_without_encryption(const uint8_t *felica, size_t felica_len, uint8_t *tx,
                                                     size_t tx_cap) {
    if (!bpreader.card_present || felica_len < 10 || memcmp(&felica[2], FELICA_IDM, sizeof(FELICA_IDM)) != 0) {
        const uint8_t data[1] = {0x01};
        return build_response(0xA1, data, sizeof(data), tx, tx_cap);
    }

    uint8_t data[14] = {0x00, 0x0C, 0x09};
    memcpy(&data[3], FELICA_IDM, sizeof(FELICA_IDM));
    data[11] = 0x00;
    data[12] = 0x00;
    (void)felica_len;
    return build_response(0xA1, data, 13, tx, tx_cap);
}

static size_t handle_felica(const uint8_t *rx, size_t rx_len, uint8_t *tx, size_t tx_cap) {
    for (size_t i = 7; i + 1 < rx_len; i++) {
        const uint8_t felica_len = rx[i];
        if (felica_len < 2 || i + felica_len > rx_len) {
            continue;
        }

        const uint8_t felica_cmd = rx[i + 1];
        if (felica_cmd == 0x06) {
            return handle_felica_read_without_encryption(&rx[i], felica_len, tx, tx_cap);
        }
        if (felica_cmd == 0x08) {
            return handle_felica_write_without_encryption(&rx[i], felica_len, tx, tx_cap);
        }
    }

    const uint8_t data[1] = {0x01};
    return build_response(0xA1, data, sizeof(data), tx, tx_cap);
}

static size_t handle_mifare(const uint8_t *rx, size_t rx_len, uint8_t *tx, size_t tx_cap) {
    if (rx_len < 10) {
        return 0;
    }

    const uint8_t subcmd = rx[8];
    const uint8_t block = rx[9];

    if (subcmd == MIFARE_CMD_AUTH_KEY_A) {
        static const uint8_t go_next[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
        if (tx_cap < sizeof(go_next)) {
            return 0;
        }
        memcpy(tx, go_next, sizeof(go_next));
        return sizeof(go_next);
    }

    if (subcmd == MIFARE_CMD_AUTH_KEY_B) {
        const uint8_t ok[1] = {0x00};
        return build_response(0x41, ok, sizeof(ok), tx, tx_cap);
    }

    if (subcmd != MIFARE_CMD_READ || block >= BPREADER_MIFARE_BLOCK_COUNT) {
        return 0;
    }

    uint8_t data[17] = {0x00};
    memcpy(&data[1], bpreader.blocks[block].block, BPREADER_MIFARE_BLOCK_SIZE);
    return build_response(0x41, data, sizeof(data), tx, tx_cap);
}

static size_t handle_cmd_06(const uint8_t *rx, size_t rx_len, uint8_t *tx, size_t tx_cap) {
    static const uint8_t data_a[8] = {0xFF, 0x3F, 0x0E, 0xF1, 0xFF, 0x3F, 0x0E, 0xF1};
    static const uint8_t data_b[11] = {0xDC, 0xF4, 0x3F, 0x11, 0x4D, 0x85, 0x61, 0xF1, 0x26, 0x6A, 0x87};

    if (rx_len > 8 && rx[8] == 0x1C) {
        return build_response(0x07, data_a, sizeof(data_a), tx, tx_cap);
    }
    return build_response(0x07, data_b, sizeof(data_b), tx, tx_cap);
}

static size_t handle_request(const uint8_t *rx, size_t rx_len, uint8_t *tx, size_t tx_cap) {
    if (rx_len == 1 && rx[0] == 0x55) {
        return 0;
    }
    if (rx_len < 7 || rx[0] != 0x00 || rx[1] != 0x00 || rx[2] != 0xFF || rx[5] != 0xD4) {
        return 0;
    }

    switch (rx[6]) {
    case 0x06:
        return handle_cmd_06(rx, rx_len, tx, tx_cap);
    case 0x08: {
        const uint8_t data[1] = {0x00};
        return build_response(0x09, data, sizeof(data), tx, tx_cap);
    }
    case 0x0C: {
        const uint8_t data[3] = {0x00, 0x06, 0x00};
        return build_response(0x0D, data, sizeof(data), tx, tx_cap);
    }
    case 0x0E:
        return build_response(0x0F, NULL, 0, tx, tx_cap);
    case 0x12:
        return build_response(0x13, NULL, 0, tx, tx_cap);
    case 0x18:
        return build_response(0x19, NULL, 0, tx, tx_cap);
    case 0x32:
        return build_response(0x33, NULL, 0, tx, tx_cap);
    case 0x40:
        return handle_mifare(rx, rx_len, tx, tx_cap);
    case 0x44: {
        const uint8_t data[2] = {0x01, 0x00};
        return build_response(0x45, data, sizeof(data), tx, tx_cap);
    }
    case 0x4A:
        return handle_poll(tx, tx_cap);
    case 0xA0:
        return handle_felica(rx, rx_len, tx, tx_cap);
    case 0x52: {
        const uint8_t data[2] = {0x01, 0x00};
        return build_response(0x53, data, sizeof(data), tx, tx_cap);
    }
    case 0x54: {
        const uint8_t data[1] = {0x00};
        return build_response(0x55, data, sizeof(data), tx, tx_cap);
    }
    default:
        return 0;
    }
}

void bpreader_serial_init(void) {
    memset(&bpreader, 0, sizeof(bpreader));
    bpreader.card_present = BPREADER_CARD_PRESENT_DEFAULT != 0;
    if (!parse_access_code(BPREADER_ACCESS_CODE_HEX, bpreader.access_code)) {
        memset(bpreader.access_code, 0, sizeof(bpreader.access_code));
    }
    populate_card();
}

void bpreader_serial_set_card_present(bool present) { bpreader.card_present = present; }

bool bpreader_serial_card_present(void) { return bpreader.card_present; }

void bpreader_serial_set_access_code(const char access_code[21]) {
    uint8_t parsed[BPREADER_CARD_BYTES];
    if (!parse_access_code(access_code, parsed)) {
        return;
    }
    memcpy(bpreader.access_code, parsed, sizeof(bpreader.access_code));
    populate_card();
}

size_t bpreader_serial_process(const uint8_t *rx, size_t rx_len, uint8_t *tx, size_t tx_cap) {
    if (!rx || !tx || rx_len == 0 || rx_len > BPREADER_FRAME_MAX) {
        return 0;
    }
    return handle_request(rx, rx_len, tx, tx_cap);
}
