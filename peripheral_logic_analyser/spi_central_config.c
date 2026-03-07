#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "spi_central_config.h"

#define SPI_CENTRAL_INST    spi0 //in SDK. means 
#define SPI_CENTRAL_MISO    16
#define SPI_CENTRAL_MOSI    19
#define SPI_CENTRAL_SCK     18
#define SPI_CENTRAL_CS      17
#define ACK_PIN             20

static bool g_spi_central_init_done = false;

void spi_central_init(void) {
    if (g_spi_central_init_done) return;

    // intialize the spi protocol to central
    spi_init(SPI_CENTRAL_INST, 10 * 1000 * 2000);
    gpio_set_function(SPI_CENTRAL_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SPI_CENTRAL_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SPI_CENTRAL_SCK, GPIO_FUNC_SPI);

    gpio_init(SPI_CENTRAL_CS);
    gpio_set_dir(SPI_CENTRAL_CS, GPIO_OUT); // 
    gpio_put(SPI_CENTRAL_CS, 1); // initially high means not selected

    g_spi_central_init_done = true;
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

bool wait_for_ack(uint32_t timeout_ms){
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while(!gpio_get(ACK_PIN)) {
        if(to_ms_since_boot(get_absolute_time()) - start > timeout_ms) return false;
    }
    while(gpio_get(ACK_PIN)) {
        tight_loop_contents();
    }
    return true;
}