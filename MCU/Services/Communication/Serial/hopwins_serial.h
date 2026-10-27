#ifndef HOPWINS_SERIAL_H
#define HOPWINS_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOPWINS_SERIAL_CRC_SIZE          4U
#define HOPWINS_SERIAL_MAX_PAYLOAD_SIZE  512U

typedef enum
{
    HOPWINS_SERIAL_STATUS_OK = 0,
    HOPWINS_SERIAL_STATUS_BUSY,
    HOPWINS_SERIAL_STATUS_INVALID_ARGUMENT,
    HOPWINS_SERIAL_STATUS_IO_ERROR
} hopwins_serial_status_t;

hopwins_serial_status_t hopwins_serial_init(void);

/* Queues payload plus a big-endian CRC-32/MPEG-2 for TX DMA. */
hopwins_serial_status_t hopwins_serial_send(const uint8_t *payload, size_t payload_size);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_SERIAL_H */
