#ifndef HOPWINS_DW1000_H
#define HOPWINS_DW1000_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hopwins_dw1000_init(void);

uint32_t hopwins_dw1000_spi_frequency_hz(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_DW1000_H */
