#include "utils/I2cDebugLog.h"

#include "GlobalConfiguration.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr uint8_t kLogAddress = 0x42;
constexpr uint8_t kSdaPin = 4;
constexpr uint8_t kSclPin = 15;
constexpr uint32_t kBackoffUs = 10'000;
constexpr uint32_t kI2cHalfPeriodUs = 10;
constexpr size_t kQueueSize = 128;
constexpr size_t kPayloadSize = 16;

struct LogPacket {
    uint8_t magic0;
    uint8_t magic1;
    uint16_t sequence;
    uint32_t time_ms;
    uint8_t event;
    uint8_t a;
    uint16_t b;
    uint16_t c;
    uint8_t length;
    uint8_t dropped;
    uint8_t payload[kPayloadSize];
};

static_assert(sizeof(LogPacket) == 32);

std::array<LogPacket, kQueueSize> log_queue{};
uint16_t write_index = 0;
uint16_t read_index = 0;
uint16_t sequence = 0;
uint8_t dropped_packets = 0;
bool initialized = false;
absolute_time_t next_attempt = nil_time;

uint16_t next_index(uint16_t index) { return static_cast<uint16_t>((index + 1) % kQueueSize); }

void sda_low() {
    gpio_put(kSdaPin, false);
    gpio_set_dir(kSdaPin, GPIO_OUT);
}

void scl_low() {
    gpio_put(kSclPin, false);
    gpio_set_dir(kSclPin, GPIO_OUT);
}

void sda_release() {
    gpio_set_dir(kSdaPin, GPIO_IN);
}

void scl_release() {
    gpio_set_dir(kSclPin, GPIO_IN);
}

void i2c_delay() {
    sleep_us(kI2cHalfPeriodUs);
}

void software_i2c_start() {
    sda_release();
    scl_release();
    i2c_delay();
    sda_low();
    i2c_delay();
    scl_low();
}

void software_i2c_stop() {
    sda_low();
    i2c_delay();
    scl_release();
    i2c_delay();
    sda_release();
    i2c_delay();
}

bool software_i2c_write_byte(uint8_t value) {
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
        if (value & mask) {
            sda_release();
        } else {
            sda_low();
        }
        i2c_delay();
        scl_release();
        i2c_delay();
        scl_low();
    }

    sda_release();
    i2c_delay();
    scl_release();
    i2c_delay();
    const bool ack = !gpio_get(kSdaPin);
    scl_low();
    return ack;
}

bool software_i2c_write_packet(const LogPacket &packet) {
    const uint8_t *data = reinterpret_cast<const uint8_t *>(&packet);
    software_i2c_start();
    bool ok = software_i2c_write_byte((uint8_t)(kLogAddress << 1));
    for (size_t i = 0; ok && i < sizeof(packet); i++) {
        ok = software_i2c_write_byte(data[i]);
    }
    software_i2c_stop();
    return ok;
}

void push_packet(uint8_t event, uint8_t a, uint16_t b, uint16_t c, const uint8_t *data, size_t size) {
    const uint32_t irq = save_and_disable_interrupts();

    const uint16_t next = next_index(write_index);
    if (next == read_index) {
        if (dropped_packets != UINT8_MAX) {
            dropped_packets++;
        }
        restore_interrupts_from_disabled(irq);
        return;
    }

    LogPacket &packet = log_queue[write_index];
    packet.magic0 = 'U';
    packet.magic1 = 'L';
    packet.sequence = sequence++;
    packet.time_ms = to_ms_since_boot(get_absolute_time());
    packet.event = event;
    packet.a = a;
    packet.b = b;
    packet.c = c;
    packet.length = static_cast<uint8_t>(std::min(size, kPayloadSize));
    packet.dropped = dropped_packets;
    dropped_packets = 0;
    std::memset(packet.payload, 0, sizeof(packet.payload));
    if (data && packet.length) {
        std::memcpy(packet.payload, data, packet.length);
    }

    write_index = next;
    restore_interrupts_from_disabled(irq);
}

} // namespace

void i2c_debug_log_init(void) {
    gpio_init(kSdaPin);
    gpio_init(kSclPin);
    gpio_put(kSdaPin, false);
    gpio_put(kSclPin, false);
    gpio_set_dir(kSdaPin, GPIO_IN);
    gpio_set_dir(kSclPin, GPIO_IN);
    gpio_pull_up(kSdaPin);
    gpio_pull_up(kSclPin);
    initialized = true;
    i2c_debug_log_event(I2C_DEBUG_LOG_EVENT_BOOT, 0, kSdaPin, kSclPin);
    i2c_debug_log_event(I2C_DEBUG_LOG_EVENT_OPEN, 0, 0, 0);
}

void i2c_debug_log_task(void) {
    if (!initialized) {
        return;
    }

    if (read_index == write_index || absolute_time_diff_us(get_absolute_time(), next_attempt) > 0) {
        return;
    }

    LogPacket packet{};
    uint32_t irq = save_and_disable_interrupts();
    packet = log_queue[read_index];
    restore_interrupts_from_disabled(irq);

    if (software_i2c_write_packet(packet)) {
        irq = save_and_disable_interrupts();
        if (read_index != write_index) {
            read_index = next_index(read_index);
        }
        restore_interrupts_from_disabled(irq);
    } else {
        next_attempt = make_timeout_time_us(kBackoffUs);
    }
}

void i2c_debug_log_event(uint8_t event, uint8_t a, uint16_t b, uint16_t c) {
    push_packet(event, a, b, c, nullptr, 0);
}

void i2c_debug_log_bytes(uint8_t event, uint8_t a, uint16_t b, uint16_t c, const uint8_t *data, size_t size) {
    push_packet(event, a, b, c, data, size);
}
