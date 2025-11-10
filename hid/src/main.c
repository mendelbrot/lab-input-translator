/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

// UART defines
#define BAUD_RATE 9600 // 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

// Interface index depends on the order in configuration descriptor
enum
{
  ITF_KEYBOARD = 0,
  ITF_MOUSE = 1
};

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void led_blinking_task(void);
void hid_task(void);
void uart_data_task(void);

/*------------- MAIN -------------*/
int main(void)
{
  board_init();

  // Set up UART
  uart_init(uart1, BAUD_RATE);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

  // init device stack on configured roothub port
  tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_AUTO};
  tusb_init(BOARD_TUD_RHPORT, &dev_init);

  if (board_init_after_tusb)
  {
    board_init_after_tusb();
  }

  while (1)
  {
    tud_task(); // tinyusb device task
    led_blinking_task();
    hid_task();
    uart_data_task();
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
  (void)remote_wakeup_en;
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

void hid_task(void)
{
  // Poll every 10ms
  const uint32_t interval_ms = 10;
  static uint32_t start_ms = 0;

  if (board_millis() - start_ms < interval_ms)
    return; // not enough time
  start_ms += interval_ms;

  uint32_t const btn = board_button_read();

  // Remote wakeup
  if (tud_suspended() && btn)
  {
    // Wake up host if we are in suspend mode
    // and REMOTE_WAKEUP feature is enabled by host
    tud_remote_wakeup();
  }

  /*------------- BUTTON PRESS DATA ENTRY TEST -------------*/
  // Press the button to test data entry to the PC.
  static uint8_t seq_idx = 0;          // Index into the key sequence
  static bool sequence_active = false; // Track if we're in the middle of sending the sequence
  static const uint8_t key_sequence[14][6] = {
      // 9
      {HID_KEY_9, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0},
      // 9
      {HID_KEY_9, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0},
      // 9
      {HID_KEY_9, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0},
      // 9
      {HID_KEY_9, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0},
      // .
      {HID_KEY_PERIOD, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0},
      // 9
      {HID_KEY_9, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0},
      // Enter
      {HID_KEY_ENTER, 0, 0, 0, 0, 0},
      {0, 0, 0, 0, 0, 0}};

  if (tud_hid_n_ready(ITF_KEYBOARD))
  {
    if (btn && !sequence_active)
    {
      // Button pressed: start sequence
      seq_idx = 0;
      sequence_active = true;
    }

    if (sequence_active && seq_idx < 14)
    {
      // Send the next report in the sequence (key press or release)
      tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, (uint8_t *)key_sequence[seq_idx]);
      seq_idx++;
    }
    if (sequence_active && seq_idx == 14 && !btn)
    {
      // Sequence is complete and button was released: send empty keycode and reset for next button push
      tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, NULL);
      sequence_active = false;
      seq_idx = 0;
    }
  }
}

/*------------- Enter data from UART -------------*/
void uart_data_task(void)
{
  static bool sent_keycode = false;  // track if a keycode was sent last, to sent en empty report next if it was
  
  // Poll every 10ms
  const uint32_t interval_ms = 10;
  static uint32_t start_ms = 0;
  if (board_millis() - start_ms < interval_ms)
    return; // not enough time
  start_ms += interval_ms;

  if (sent_keycode) {
    sent_keycode = false;
    tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, NULL);
    return;
  }

  if (uart_is_readable(uart1) && tud_hid_n_ready(ITF_KEYBOARD))
  {
    uint8_t keycode[6] = {0};
    char ch = uart_getc(uart1); // Read character from UART

    switch (ch)
    {
    case '0':
      keycode[0] = HID_KEY_0;
      break;
    case '1':
      keycode[0] = HID_KEY_1;
      break;
    case '2':
      keycode[0] = HID_KEY_2;
      break;
    case '3':
      keycode[0] = HID_KEY_3;
      break;
    case '4':
      keycode[0] = HID_KEY_4;
      break;
    case '5':
      keycode[0] = HID_KEY_5;
      break;
    case '6':
      keycode[0] = HID_KEY_6;
      break;
    case '7':
      keycode[0] = HID_KEY_7;
      break;
    case '8':
      keycode[0] = HID_KEY_8;
      break;
    case '9':
      keycode[0] = HID_KEY_9;
      break;
    case '.':
      keycode[0] = HID_KEY_PERIOD;
      break;
    case '\n':
      keycode[0] = HID_KEY_ENTER;
      break;
    default:
      break; // Ignore unsupported characters
    }

    if (keycode[0] != 0)
    {
      tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, keycode);
      sent_keycode = true;
    }
  }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void)itf;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
  // TODO set LED based on CAPLOCK, NUMLOCK etc...
  (void)itf;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Blink every interval ms
  if (board_millis() - start_ms < blink_interval_ms)
    return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
