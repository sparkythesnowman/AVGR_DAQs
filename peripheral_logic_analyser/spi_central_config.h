#ifndef SPI_CENTRAL_CONFIG_H
#define SPI_CENTRAL_CONFIG_H

#include <stdbool.h> //
#include <stddef.h> //
#include <stdint.h>

void spi_central_init(void);
void spi_central_signal_ready(void);
bool spi_central_send(const void *data, size_t len);
bool spi_central_ready(void);
bool wait_for_ack(uint32_t timeout_ms);

#endif
