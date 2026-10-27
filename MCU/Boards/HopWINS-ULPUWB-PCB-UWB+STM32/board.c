#include "board.h"

#define HOPWINS_BOARD_DW1000_SPI_LOW_PRESCALER   SPI_BAUDRATEPRESCALER_16
#define HOPWINS_BOARD_DW1000_SPI_HIGH_PRESCALER  SPI_BAUDRATEPRESCALER_2

static bool hopwins_board_dw1000_spi_prescaler(
    hopwins_board_dw1000_spi_speed_t speed,
    uint32_t *prescaler)
{
    if (prescaler == NULL)
    {
        return false;
    }

    switch (speed)
    {
        case HOPWINS_BOARD_DW1000_SPI_SPEED_LOW:
            *prescaler = HOPWINS_BOARD_DW1000_SPI_LOW_PRESCALER;
            return true;

        case HOPWINS_BOARD_DW1000_SPI_SPEED_HIGH:
            *prescaler = HOPWINS_BOARD_DW1000_SPI_HIGH_PRESCALER;
            return true;

        default:
            return false;
    }
}

static uint32_t hopwins_board_dw1000_spi_frequency_from_prescaler(uint32_t prescaler)
{
    uint32_t divisor = 2UL << ((prescaler & SPI_CR1_BR) >> SPI_CR1_BR_Pos);

    return HAL_RCC_GetPCLK1Freq() / divisor;
}

bool hopwins_board_dw1000_spi_set_speed(hopwins_board_dw1000_spi_speed_t speed)
{
    SPI_HandleTypeDef *spi = HOPWINS_BOARD_DW1000_SPI_HANDLE;
    uint32_t prescaler;
    uint32_t spi_was_enabled;

    if (!hopwins_board_dw1000_spi_prescaler(speed, &prescaler) ||
        (HAL_SPI_GetState(spi) != HAL_SPI_STATE_READY))
    {
        return false;
    }

    spi_was_enabled = READ_BIT(spi->Instance->CR1, SPI_CR1_SPE);
    __HAL_SPI_DISABLE(spi);
    MODIFY_REG(spi->Instance->CR1, SPI_CR1_BR, prescaler);
    spi->Init.BaudRatePrescaler = prescaler;

    if (spi_was_enabled != 0U)
    {
        __HAL_SPI_ENABLE(spi);
    }

    return true;
}

uint32_t hopwins_board_dw1000_spi_speed_frequency_hz(
    hopwins_board_dw1000_spi_speed_t speed)
{
    uint32_t prescaler;

    if (!hopwins_board_dw1000_spi_prescaler(speed, &prescaler))
    {
        return 0U;
    }

    return hopwins_board_dw1000_spi_frequency_from_prescaler(prescaler);
}

uint32_t hopwins_board_dw1000_spi_frequency_hz(void)
{
    uint32_t prescaler =
        READ_BIT(HOPWINS_BOARD_DW1000_SPI_HANDLE->Instance->CR1, SPI_CR1_BR);

    return hopwins_board_dw1000_spi_frequency_from_prescaler(prescaler);
}
