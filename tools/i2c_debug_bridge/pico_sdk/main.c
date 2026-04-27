#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "pico/i2c_slave.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    I2C_ADDR = 0x42,
    I2C_SDA_PIN = 4,
    I2C_SCL_PIN = 5,
    I2C_BAUDRATE = 1000000,
    PACKET_SIZE = 32,
    RING_SIZE = 64,
};

static uint8_t current_packet[PACKET_SIZE];
static uint8_t packets[RING_SIZE][PACKET_SIZE];
static volatile uint8_t current_len = 0;
static volatile uint8_t ring_read = 0;
static volatile uint8_t ring_write = 0;
static volatile uint8_t dropped_packets = 0;
static volatile uint32_t short_packets = 0;

static uint8_t next_index(uint8_t index) {
    return (uint8_t)((index + 1) % RING_SIZE);
}

static void queue_packet_from_isr(void) {
    if (current_packet[0] != 'U' || current_packet[1] != 'L') {
        return;
    }

    const uint8_t next = next_index(ring_write);
    if (next == ring_read) {
        if (dropped_packets != UINT8_MAX) {
            dropped_packets++;
        }
        return;
    }

    for (uint8_t i = 0; i < PACKET_SIZE; i++) {
        packets[ring_write][i] = current_packet[i];
    }
    ring_write = next;
}

static void i2c_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    switch (event) {
    case I2C_SLAVE_RECEIVE:
        while (i2c_get_read_available(i2c)) {
            const uint8_t byte = i2c_read_byte_raw(i2c);
            if (current_len < PACKET_SIZE) {
                current_packet[current_len++] = byte;
            }
        }
        break;

    case I2C_SLAVE_REQUEST:
        if (i2c_get_write_available(i2c)) {
            i2c_write_byte_raw(i2c, 0);
        }
        break;

    case I2C_SLAVE_FINISH:
        if (current_len == PACKET_SIZE) {
            queue_packet_from_isr();
        } else if (current_len != 0) {
            short_packets++;
        }
        current_len = 0;
        break;
    }
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const char *event_name(uint8_t event) {
    switch (event) {
    case 0x01:
        return "BOOT";
    case 0x10:
        return "OPEN";
    case 0x11:
        return "RESET";
    case 0x12:
        return "IDLE_ZLP";
    case 0x20:
        return "OUT";
    case 0x21:
        return "CMD_READ";
    case 0x22:
        return "CMD_WRITE";
    case 0x23:
        return "CMD_INIT";
    case 0x30:
        return "IN_DATA";
    case 0x31:
        return "IN_ZLP_0";
    case 0x32:
        return "IN_ZLP_TERM";
    case 0x33:
        return "IN_DONE";
    case 0x40:
        return "STATUS_ARM";
    case 0x41:
        return "STATUS_DONE";
    default:
        return "UNKNOWN";
    }
}

static bool pop_packet(uint8_t out[PACKET_SIZE], uint8_t *dropped, uint32_t *short_count) {
    const uint32_t irq = save_and_disable_interrupts();
    if (ring_read == ring_write) {
        restore_interrupts_from_disabled(irq);
        return false;
    }

    for (uint8_t i = 0; i < PACKET_SIZE; i++) {
        out[i] = packets[ring_read][i];
    }
    ring_read = next_index(ring_read);
    *dropped = dropped_packets;
    dropped_packets = 0;
    *short_count = short_packets;
    restore_interrupts_from_disabled(irq);
    return true;
}

static void print_packet(const uint8_t packet[PACKET_SIZE], uint8_t bridge_dropped, uint32_t short_count) {
    const uint16_t seq = rd16(&packet[2]);
    const uint32_t ms = rd32(&packet[4]);
    const uint8_t event = packet[8];
    const uint8_t a = packet[9];
    const uint16_t b = rd16(&packet[10]);
    const uint16_t c = rd16(&packet[12]);
    const uint8_t len = packet[14] > 16 ? 16 : packet[14];
    const uint8_t firmware_dropped = packet[15];

    printf("%10lu seq=%5u event=0x%02x %-12s a=0x%02x b=0x%04x c=0x%04x fw_drop=%u bridge_drop=%u short=%lu data=",
           (unsigned long)ms, seq, event, event_name(event), a, b, c, firmware_dropped, bridge_dropped,
           (unsigned long)short_count);

    for (uint8_t i = 0; i < len; i++) {
        printf("%02x", packet[16 + i]);
        if (i + 1 < len) {
            putchar(' ');
        }
    }
    putchar('\n');
}

int main(void) {
    stdio_init_all();

    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    i2c_init(i2c0, I2C_BAUDRATE);
    i2c_slave_init(i2c0, I2C_ADDR, i2c_handler);

    printf("ITAIKO I2C debug bridge ready addr=0x%02x sda=%u scl=%u baud=%u\n", I2C_ADDR, I2C_SDA_PIN, I2C_SCL_PIN,
           I2C_BAUDRATE);

    while (true) {
        uint8_t packet[PACKET_SIZE];
        uint8_t bridge_dropped = 0;
        uint32_t short_count = 0;
        if (pop_packet(packet, &bridge_dropped, &short_count)) {
            print_packet(packet, bridge_dropped, short_count);
        } else {
            tight_loop_contents();
        }
    }
}
