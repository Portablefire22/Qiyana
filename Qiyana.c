#include <stdlib.h>
#include <stdio.h>
#include "pico/cyw43_arch.h"

#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

#define RowCount 6
#define ColumnCount 17
#define QueueMax 32

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void led_blinking_task(void);
void hid_task(void);

// Apparently this is by GPIO and not Pin, which ofc are different
// because why not.
// Matrix Row GPIO in order 1 - 6
static const uint16_t ROWS[RowCount] = {17, 18, 19, 20, 21, 22};

// Matrix column GPIO
static const uint16_t COLUMNS[ColumnCount] = {0, 1, 2, 3, 
                         4, 5 ,6 ,7, 
                         8, 9, 10, 11, 
                         12, 13, 14, 15, 16};

// Holds keycodes in corresponding to their key position.
static uint32_t KeyMap[RowCount * ColumnCount];

static uint32_t Queue[QueueMax];

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

void read_all_pins() {
  int queuePos = 0;
  for (int column = 0; column < ColumnCount; column++) {
    gpio_put(COLUMNS[column], 1);
    for (int row = 0; row < RowCount; row++) {
      if (!read_pin(ROWS[row])) continue;
      Queue[queuePos++] = KeyMap[row + (column * ColumnCount)];
      if (queuePos > QueueMax) return;
    }
    gpio_put(COLUMNS[column], 0);
  }
  Queue[queuePos] = HID_KEY_NONE;
}

int main()
{
    // initialise tinyUSB

    board_init();
    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb){
        board_init_after_tusb();
    }

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    for (int i = 0; i < QueueMax; i++) {
      Queue[i] = HID_KEY_NONE;
    }

    init_pins();

    while (true) {
        tud_task();
        read_all_pins();
        hid_task();
    }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

static void send_hid_report(uint8_t report_id, uint32_t btn)
{
  // skip if hid is not ready yet
  if ( !tud_hid_ready() ) return;

  switch(report_id)
  {
    case REPORT_ID_KEYBOARD:
    {
      // use to avoid send multiple consecutive zero report for keyboard
      static bool has_keyboard_key = false;

      if ( btn )
      {
        uint8_t keycode[6] = { 0 };
        keycode[0] = HID_KEY_A;

        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, keycode);
        has_keyboard_key = true;
      }else
      {
        // send empty key report if previously has key pressed
        if (has_keyboard_key) tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        has_keyboard_key = false;
      }
    }
    break;
    default: break;
  }
}

// Every 10ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
  // Poll every 10ms
  const uint32_t interval_ms = 10;
  static uint32_t start_ms = 0;

  if ( board_millis() - start_ms < interval_ms) return; // not enough time
  start_ms += interval_ms;

  for (int i = 0; i < QueueMax; i++) {
    uint32_t key = Queue[i];
    if (key == 0x0) break; // Early end of Queue is marked with 0x00;
    // Remote wakeup
    if ( tud_suspended() && key)
    {
      // Wake up host if we are in suspend mode
      // and REMOTE_WAKEUP feature is enabled by host
      tud_remote_wakeup();
    }else
    {
      send_hid_report(REPORT_ID_KEYBOARD, key);
    }
  }
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) instance;
  (void) len;

  uint8_t next_report_id = report[0] + 1u;

  if (next_report_id < REPORT_ID_COUNT)
  {
    send_hid_report(next_report_id, board_button_read());
  }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD)
    {
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
      {
        // Capslock On: disable blink, turn led on
        blink_interval_ms = 0;
        board_led_write(true);
      }else
      {
        // Caplocks Off: back to normal blink
        board_led_write(false);
        blink_interval_ms = BLINK_MOUNTED;
      }
    }
  }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // blink is disabled
  if (!blink_interval_ms) return;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
  led_state = 1 - led_state; // toggle
}
