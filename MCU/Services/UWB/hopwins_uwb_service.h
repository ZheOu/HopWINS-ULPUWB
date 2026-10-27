#ifndef HOPWINS_UWB_SERVICE_H
#define HOPWINS_UWB_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool hopwins_uwb_init(void);

bool hopwins_uwb_periodic_tx_start(void);
void hopwins_uwb_periodic_tx_process(void);

bool hopwins_uwb_rx_start(void);
void hopwins_uwb_rx_process(void);

#ifdef __cplusplus
}
#endif

#endif /* HOPWINS_UWB_SERVICE_H */
