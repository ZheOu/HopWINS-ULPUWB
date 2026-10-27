#include "hopwins_serial.h"

#include <string.h>

#include "board.h"

#define HOPWINS_SERIAL_CRC32_POLYNOMIAL  0x04C11DB7UL
#define HOPWINS_SERIAL_TX_QUEUE_DEPTH    2U

_Static_assert((HOPWINS_SERIAL_MAX_PAYLOAD_SIZE + HOPWINS_SERIAL_CRC_SIZE) <= UINT16_MAX,
               "UART DMA length exceeds HAL limit");

extern CRC_HandleTypeDef hcrc;

static uint8_t hopwins_serial_tx_buffer[HOPWINS_SERIAL_TX_QUEUE_DEPTH]
                                       [HOPWINS_SERIAL_MAX_PAYLOAD_SIZE + HOPWINS_SERIAL_CRC_SIZE];
static uint16_t hopwins_serial_tx_length[HOPWINS_SERIAL_TX_QUEUE_DEPTH];
static volatile uint8_t hopwins_serial_tx_head;
static volatile uint8_t hopwins_serial_tx_count;

static uint32_t hopwins_serial_irq_save(void)
{
    uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();
    return interrupt_state;
}

static void hopwins_serial_irq_restore(uint32_t interrupt_state)
{
    if (interrupt_state == 0U)
    {
        __enable_irq();
    }
}

static uint32_t hopwins_serial_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc;

    __HAL_CRC_DR_RESET(&hcrc);

    while (size >= sizeof(uint32_t))
    {
        uint32_t word = ((uint32_t)data[0] << 24U) |
                        ((uint32_t)data[1] << 16U) |
                        ((uint32_t)data[2] << 8U) |
                        (uint32_t)data[3];

        hcrc.Instance->DR = word;
        data += sizeof(uint32_t);
        size -= sizeof(uint32_t);
    }

    crc = hcrc.Instance->DR;

    while (size != 0U)
    {
        uint32_t bit;

        crc ^= (uint32_t)(*data) << 24U;
        ++data;
        --size;

        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 0x80000000UL) != 0U)
                      ? (crc << 1U) ^ HOPWINS_SERIAL_CRC32_POLYNOMIAL
                      : crc << 1U;
        }
    }

    return crc;
}

static HAL_StatusTypeDef hopwins_serial_start_current(void)
{
    return HAL_UART_Transmit_DMA(HOPWINS_BOARD_PC_SERIAL_UART_HANDLE,
                                 hopwins_serial_tx_buffer[hopwins_serial_tx_head],
                                 hopwins_serial_tx_length[hopwins_serial_tx_head]);
}

hopwins_serial_status_t hopwins_serial_init(void)
{
    if ((HOPWINS_BOARD_PC_SERIAL_UART_HANDLE->hdmatx == NULL) ||
        (HOPWINS_BOARD_PC_SERIAL_UART_HANDLE->hdmatx->Init.Mode != DMA_NORMAL) ||
        (HAL_UART_GetState(HOPWINS_BOARD_PC_SERIAL_UART_HANDLE) != HAL_UART_STATE_READY) ||
        (HAL_CRC_GetState(&hcrc) != HAL_CRC_STATE_READY))
    {
        return HOPWINS_SERIAL_STATUS_IO_ERROR;
    }

    hopwins_serial_tx_head = 0U;
    hopwins_serial_tx_count = 0U;
    return HOPWINS_SERIAL_STATUS_OK;
}

hopwins_serial_status_t hopwins_serial_send(const uint8_t *payload, size_t payload_size)
{
    uint32_t interrupt_state;
    uint32_t crc;
    uint8_t buffer_index;

    if ((payload == NULL) || (payload_size == 0U) ||
        (payload_size > HOPWINS_SERIAL_MAX_PAYLOAD_SIZE))
    {
        return HOPWINS_SERIAL_STATUS_INVALID_ARGUMENT;
    }

    crc = hopwins_serial_crc32(payload, payload_size);
    interrupt_state = hopwins_serial_irq_save();

    if (hopwins_serial_tx_count >= HOPWINS_SERIAL_TX_QUEUE_DEPTH)
    {
        hopwins_serial_irq_restore(interrupt_state);
        return HOPWINS_SERIAL_STATUS_BUSY;
    }

    buffer_index = (uint8_t)((hopwins_serial_tx_head + hopwins_serial_tx_count) %
                             HOPWINS_SERIAL_TX_QUEUE_DEPTH);
    memcpy(hopwins_serial_tx_buffer[buffer_index], payload, payload_size);
    hopwins_serial_tx_buffer[buffer_index][payload_size] = (uint8_t)(crc >> 24U);
    hopwins_serial_tx_buffer[buffer_index][payload_size + 1U] = (uint8_t)(crc >> 16U);
    hopwins_serial_tx_buffer[buffer_index][payload_size + 2U] = (uint8_t)(crc >> 8U);
    hopwins_serial_tx_buffer[buffer_index][payload_size + 3U] = (uint8_t)crc;
    hopwins_serial_tx_length[buffer_index] =
        (uint16_t)(payload_size + HOPWINS_SERIAL_CRC_SIZE);
    ++hopwins_serial_tx_count;

    if ((hopwins_serial_tx_count == 1U) &&
        (hopwins_serial_start_current() != HAL_OK))
    {
        hopwins_serial_tx_count = 0U;
        hopwins_serial_irq_restore(interrupt_state);
        return HOPWINS_SERIAL_STATUS_IO_ERROR;
    }

    hopwins_serial_irq_restore(interrupt_state);

    return HOPWINS_SERIAL_STATUS_OK;
}

/* Function names below are fixed by the STM32 HAL callback interface. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == HOPWINS_BOARD_PC_SERIAL_UART_HANDLE)
    {
        if (hopwins_serial_tx_count != 0U)
        {
            hopwins_serial_tx_head =
                (uint8_t)((hopwins_serial_tx_head + 1U) % HOPWINS_SERIAL_TX_QUEUE_DEPTH);
            --hopwins_serial_tx_count;
        }

        if ((hopwins_serial_tx_count != 0U) &&
            (hopwins_serial_start_current() != HAL_OK))
        {
            hopwins_serial_tx_count = 0U;
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart == HOPWINS_BOARD_PC_SERIAL_UART_HANDLE)
    {
        hopwins_serial_tx_count = 0U;
    }
}
