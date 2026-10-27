#include "hopwins_dw1000.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "deca_device_api.h"
#include "deca_regs.h"

#define DW1000_INITIALISATION_SPI_MAX_HZ   3000000UL
#define DW1000_RUNTIME_SPI_MAX_HZ          20000000UL
#define DW1000_PLL_LOCK_TIMEOUT_MS         5U
#define DW1000_SPI_TRANSACTION_TIMEOUT_MS  100U
#define DW1000_INITIALISATION_OPTIONS      DWT_LOADUCODE

_Static_assert(sizeof(uint8) == 1U, "Decawave uint8 must be 8 bits");
_Static_assert(sizeof(uint16) == 2U, "Decawave uint16 must be 16 bits");
_Static_assert(sizeof(uint32) == 4U, "Decawave uint32 must be 32 bits");

static void hopwins_dw1000_chip_select(GPIO_PinState state)
{
    HAL_GPIO_WritePin(HOPWINS_BOARD_DW1000_CHIP_SELECT_GPIO_PORT,
                      HOPWINS_BOARD_DW1000_CHIP_SELECT_GPIO_PIN,
                      state);
}

static int hopwins_dw1000_use_initialisation_spi(void)
{
    uint32_t frequency_hz = hopwins_board_dw1000_spi_speed_frequency_hz(
        HOPWINS_BOARD_DW1000_SPI_SPEED_LOW);

    if ((frequency_hz == 0U) || (frequency_hz >= DW1000_INITIALISATION_SPI_MAX_HZ))
    {
        return DWT_ERROR;
    }

    return hopwins_board_dw1000_spi_set_speed(HOPWINS_BOARD_DW1000_SPI_SPEED_LOW)
               ? DWT_SUCCESS
               : DWT_ERROR;
}

static int hopwins_dw1000_use_runtime_spi(void)
{
    uint32_t frequency_hz = hopwins_board_dw1000_spi_speed_frequency_hz(
        HOPWINS_BOARD_DW1000_SPI_SPEED_HIGH);
    uint32_t wait_started = HAL_GetTick();

    if ((frequency_hz == 0U) || (frequency_hz > DW1000_RUNTIME_SPI_MAX_HZ))
    {
        return DWT_ERROR;
    }

    while ((dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_CPLOCK) == 0U)
    {
        if ((HAL_GetTick() - wait_started) >= DW1000_PLL_LOCK_TIMEOUT_MS)
        {
            return DWT_ERROR;
        }
    }

    return hopwins_board_dw1000_spi_set_speed(HOPWINS_BOARD_DW1000_SPI_SPEED_HIGH)
               ? DWT_SUCCESS
               : DWT_ERROR;
}

int hopwins_dw1000_init(void)
{
    int status;

    status = hopwins_dw1000_use_initialisation_spi();
    if (status != DWT_SUCCESS)
    {
        return status;
    }

    status = dwt_initialise(DW1000_INITIALISATION_OPTIONS);
    if (status != DWT_SUCCESS)
    {
        return status;
    }

    return hopwins_dw1000_use_runtime_spi();
}

uint32_t hopwins_dw1000_spi_frequency_hz(void)
{
    return hopwins_board_dw1000_spi_frequency_hz();
}

/* Function names below are fixed by the Decawave platform interface. */
int writetospi(uint16 headerLength, const uint8 *headerBuffer, uint32 bodylength, const uint8 *bodyBuffer)
{
    HAL_StatusTypeDef status;

    if ((headerLength == 0U) || (headerBuffer == NULL) || (bodylength > UINT16_MAX) ||
        ((bodylength != 0U) && (bodyBuffer == NULL)))
    {
        return DWT_ERROR;
    }

    hopwins_dw1000_chip_select(GPIO_PIN_RESET);
    status = HAL_SPI_Transmit(HOPWINS_BOARD_DW1000_SPI_HANDLE,
                              (uint8_t *)headerBuffer,
                              headerLength,
                              DW1000_SPI_TRANSACTION_TIMEOUT_MS);

    if ((status == HAL_OK) && (bodylength != 0U))
    {
        status = HAL_SPI_Transmit(HOPWINS_BOARD_DW1000_SPI_HANDLE,
                                  (uint8_t *)bodyBuffer,
                                  (uint16_t)bodylength,
                                  DW1000_SPI_TRANSACTION_TIMEOUT_MS);
    }

    hopwins_dw1000_chip_select(GPIO_PIN_SET);
    return (status == HAL_OK) ? DWT_SUCCESS : DWT_ERROR;
}

int readfromspi(uint16 headerLength, const uint8 *headerBuffer, uint32 readlength, uint8 *readBuffer)
{
    HAL_StatusTypeDef status;

    if ((headerLength == 0U) || (headerBuffer == NULL) || (readlength > UINT16_MAX) ||
        ((readlength != 0U) && (readBuffer == NULL)))
    {
        return DWT_ERROR;
    }

    if (readlength != 0U)
    {
        memset(readBuffer, 0, readlength);
    }

    hopwins_dw1000_chip_select(GPIO_PIN_RESET);
    status = HAL_SPI_Transmit(HOPWINS_BOARD_DW1000_SPI_HANDLE,
                              (uint8_t *)headerBuffer,
                              headerLength,
                              DW1000_SPI_TRANSACTION_TIMEOUT_MS);

    if ((status == HAL_OK) && (readlength != 0U))
    {
        status = HAL_SPI_Receive(HOPWINS_BOARD_DW1000_SPI_HANDLE,
                                 readBuffer,
                                 (uint16_t)readlength,
                                 DW1000_SPI_TRANSACTION_TIMEOUT_MS);
    }

    hopwins_dw1000_chip_select(GPIO_PIN_SET);
    return (status == HAL_OK) ? DWT_SUCCESS : DWT_ERROR;
}

decaIrqStatus_t decamutexon(void)
{
    return (decaIrqStatus_t)0U;
}

void decamutexoff(decaIrqStatus_t interrupt_state)
{
    (void)interrupt_state;
}

void deca_sleep(unsigned int time_ms)
{
    HAL_Delay(time_ms);
}
