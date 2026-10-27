#ifndef HOPWINS_ULPUWB_BOARD_H
#define HOPWINS_ULPUWB_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SPI_HandleTypeDef hspi2;
extern UART_HandleTypeDef huart1;

typedef enum
{
    HOPWINS_BOARD_DW1000_SPI_SPEED_LOW = 0,
    HOPWINS_BOARD_DW1000_SPI_SPEED_HIGH
} hopwins_board_dw1000_spi_speed_t;

/* Low/high map to this board's two fixed STM32 SPI prescalers. */
bool hopwins_board_dw1000_spi_set_speed(hopwins_board_dw1000_spi_speed_t speed);
uint32_t hopwins_board_dw1000_spi_speed_frequency_hz(
    hopwins_board_dw1000_spi_speed_t speed);
/* Returns the frequency selected in the SPI peripheral register. */
uint32_t hopwins_board_dw1000_spi_frequency_hz(void);

#ifdef __cplusplus
}
#endif

#define HOPWINS_BOARD_DW1000_SPI_HANDLE             (&hspi2)
#define HOPWINS_BOARD_DW1000_CHIP_SELECT_GPIO_PORT  SPI2_CS_GPIO_Port
#define HOPWINS_BOARD_DW1000_CHIP_SELECT_GPIO_PIN   SPI2_CS_Pin

#define HOPWINS_BOARD_PC_SERIAL_UART_HANDLE         (&huart1)

#endif /* HOPWINS_ULPUWB_BOARD_H */
