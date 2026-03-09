#include <stdio.h>
#include "hardware/spi.h"
#include "hardware/structs/spi.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "spi_central_config.h"

#define SPI_CENTRAL_INST    spi1
#define SPI_CENTRAL_MISO    12  // spi1 RX
#define SPI_CENTRAL_MOSI    15  // spi1 TX
#define SPI_CENTRAL_SCK     14  // spi1 SCK
#define SPI_CENTRAL_CS      13  // spi1 CSn
#define ACK_PIN             10

static bool g_spi_central_init_done = false;

void spi_central_init(void) {
    if (g_spi_central_init_done) return;

    // Initialize SPI as SLAVE — central is master and drives SCK
    // PL022 requires SSE=0 to change MS bit, so: init → disable → set slave → re-enable
    spi_init(SPI_CENTRAL_INST, 100 * 1000);
    hw_clear_bits(&spi_get_hw(SPI_CENTRAL_INST)->cr1, SPI_SSPCR1_SSE_BITS); // disable SSE
    spi_set_slave(SPI_CENTRAL_INST, true);                                    // now MS write takes effect
    spi_set_format(SPI_CENTRAL_INST, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    hw_set_bits(&spi_get_hw(SPI_CENTRAL_INST)->cr1, SPI_SSPCR1_SSE_BITS);   // re-enable SSE

    gpio_set_function(SPI_CENTRAL_MISO, GPIO_FUNC_SPI);  // GP12 = spi1 RX (unused in slave TX mode)
    gpio_set_function(SPI_CENTRAL_MOSI, GPIO_FUNC_SPI);  // GP15 = spi1 TX (slave sends data here)
    gpio_set_function(SPI_CENTRAL_SCK, GPIO_FUNC_SPI);   // GP14 = spi1 SCK (input from master)
    gpio_set_function(SPI_CENTRAL_CS, GPIO_FUNC_SPI);    // GP13 = spi1 CSn (input from master)

    // ACK pin: start as input (idle), peripheral drives low before sending
    gpio_init(ACK_PIN);
    gpio_set_dir(ACK_PIN, GPIO_IN);
    gpio_pull_up(ACK_PIN); // idle high

    g_spi_central_init_done = true;
}

// Drive ACK low to signal "data ready" to central
void spi_central_signal_ready(void) {
    gpio_set_dir(ACK_PIN, GPIO_OUT);
    gpio_put(ACK_PIN, 0); // LOW = data ready
}

bool spi_central_send(const void *data, size_t len) {
    if (!g_spi_central_init_done) return false; // if not initialized, return false

    //---- initialized and ready to send ----
    gpio_put(SPI_CENTRAL_CS, 0); // select the central
    spi_write_blocking(SPI_CENTRAL_INST, data, len); // def in SDK. for transfer over MISO
    gpio_put(SPI_CENTRAL_CS, 1); // deselect the central
    // NEED ERROR HANDLING HERE (reference old)
    return true;
}

bool spi_central_ready(void){ // basically get function for if initiazlied
    return g_spi_central_init_done;
}

// After sending data, release ACK to input and wait for central to drive HIGH
bool wait_for_ack(uint32_t timeout_ms){
    // Release ACK line so central can drive it
    gpio_set_dir(ACK_PIN, GPIO_IN);
    gpio_pull_down(ACK_PIN); // no pull-up, wait for central to actively drive high

    // Wait for central to drive ACK HIGH = "received"
    uint32_t start = to_ms_since_boot(get_absolute_time());
    uint32_t last_print = start;
    while(!gpio_get(ACK_PIN)) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if(now - start > timeout_ms) return false;
        // Print ACK pin state every 1 second
        if(now - last_print >= 1000) {
            printf("  ACK pin (GPIO %d): %d (waiting for HIGH)\n", ACK_PIN, gpio_get(ACK_PIN));
            last_print = now;
        }
    }
    printf("  ACK pin (GPIO %d): %d (received)\n", ACK_PIN, gpio_get(ACK_PIN));
    return true;
}
