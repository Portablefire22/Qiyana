#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/uart.h"


// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define BAUD_RATE 115200

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4
#define UART_RX_PIN 5



// Apparently this is by GPIO and not Pin, which ofc are different
// because why not.

// Matrix Row GPIO in order 1 - 6
const int ROWS[6] = {17, 18, 19, 20, 21, 22};

// Matrix column GPIO
const int COLUMNS[17] = {0, 1, 2, 3, 
                         4, 5 ,6 ,7, 
                         8, 9, 10, 11, 
                         12, 13, 14, 15, 16};

/// @brief Init all defined ROWS and COLUMNS pins, setting all 
/// COLUMNS pins to be pulled down and output low.
void init_pins() {
    int rowNum = sizeof(ROWS)/sizeof(ROWS[0]);
    for (int i = 0; i < rowNum; i++) {
        gpio_init(ROWS[i]);
        gpio_pull_down(ROWS[i]);
        gpio_set_dir(ROWS[i], GPIO_IN);
    }
    int colNum = sizeof(COLUMNS)/sizeof(COLUMNS[0]);
    for (int i = 0; i < colNum; i++) {
        uint col = COLUMNS[i];
        gpio_init(col);
        gpio_set_dir(col, GPIO_OUT);
        gpio_put(col, 0);
    }
}

// I REALLY hate working with hardware :D 
// https://github.com/raspberrypi/pico-sdk/issues/1914
// https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#%5B%7B%22num%22%3A1369%2C%22gen%22%3A0%7D%2C%7B%22name%22%3A%22XYZ%22%7D%2C115%2C841.89%2Cnull%5D
// TLDR: Internal pull-downs dont work and will latch to on,
// the only software fix is to disable the pin and re-enable it 
// before checking :)
uint read_pin(uint pin) {
    gpio_set_input_enabled(pin, true);
    uint dat = gpio_get(pin);
    gpio_set_input_enabled(pin, false);
    return dat;
}

int main()
{
    stdio_init_all();

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    init_pins();

    while (true) {
        gpio_put(COLUMNS[0], 1);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        if (read_pin(ROWS[0])) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        } 
        sleep_ms(100);
    }
}
