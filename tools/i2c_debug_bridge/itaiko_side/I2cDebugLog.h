#ifndef UTILS_I2CDEBUGLOG_H_
#define UTILS_I2CDEBUGLOG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    I2C_DEBUG_LOG_EVENT_BOOT = 0x01,
    I2C_DEBUG_LOG_EVENT_OPEN = 0x10,
    I2C_DEBUG_LOG_EVENT_RESET = 0x11,
    I2C_DEBUG_LOG_EVENT_IDLE_ZLP = 0x12,
    I2C_DEBUG_LOG_EVENT_OUT = 0x20,
    I2C_DEBUG_LOG_EVENT_CMD_READ = 0x21,
    I2C_DEBUG_LOG_EVENT_CMD_WRITE = 0x22,
    I2C_DEBUG_LOG_EVENT_CMD_INIT = 0x23,
    I2C_DEBUG_LOG_EVENT_IN_DATA = 0x30,
    I2C_DEBUG_LOG_EVENT_IN_ZLP_ZERO = 0x31,
    I2C_DEBUG_LOG_EVENT_IN_ZLP_TERM = 0x32,
    I2C_DEBUG_LOG_EVENT_IN_DONE = 0x33,
    I2C_DEBUG_LOG_EVENT_STATUS_ARM = 0x40,
    I2C_DEBUG_LOG_EVENT_STATUS_DONE = 0x41,
};

void i2c_debug_log_init(void);
void i2c_debug_log_task(void);
void i2c_debug_log_event(uint8_t event, uint8_t a, uint16_t b, uint16_t c);
void i2c_debug_log_bytes(uint8_t event, uint8_t a, uint16_t b, uint16_t c, const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // UTILS_I2CDEBUGLOG_H_
