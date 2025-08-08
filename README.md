# Lab Input Translator (LIT)

The purpose of the LIT to perform a translation, allowing a lab instrument to enter reading data into a PC. The instrument is capable of saving data to a USB storage device in the form of a CSV file, but can not enter data directly to a PC. The LIT connects to the lab instrument and the PC. It allows to instrument to save a file to it, then it reads the file and sends the readings data to the PC.

The LIT consists of two Raspberry Pi Pico microcontrollers. Both of the controllers are configured as USB devices. Controller A is configured as a storage device (MSC) and is connected to it's host, the lab instrument (LI). Controller B is configured as an input device (HID) and is connected to it's host, the PC.  Device A sends data to device B using the UART (Universal Asynchronous Receiver/Transmitter) serial protocol.

```ASCII
LI --USB--> [A --UART--> B] --USB--> PC
```

## The TinyUSB Library

Both devices are set up based on examples from the TinyUSB C library as a starting point. The examples used are `cdc_msc` and `hid_multiple_interface`

https://github.com/hathach/tinyusb
https://github.com/hathach/tinyusb/tree/master/examples/device/cdc_msc
https://github.com/hathach/tinyusb/tree/master/examples/device/hid_multiple_interface
https://docs.tinyusb.org/en/latest/reference/getting_started.html
https://www.pschatzmann.ch/home/2021/02/19/tinyusb-a-simple-tutorial/

## Device A (MSC)

In the tinyUSB example named cdc_msc, the microcontroller is configured as a storage device with a filesystem.

For the LIT, the function tud_msc_write10_cb in msc_disk.c has been re-written. This function was originally a callback that saves data to a memory address. 

The function has been changed to check if the data is CSV text, and if it is, extract a cell specified by row and column values, and send this cell data to microcontroller B.

```C
const int ROW = 1;
const int COL = 2;
// Callback invoked when received WRITE10 command.
//
// For the LIT, this function is edited to extract the CSV data. 
// The data is sent to microcontroller B via UART.
// The data/file is not saved.
// -> Edit the constants COL and ROW to select the specific cell to send.
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  (void) lun; // Unused in input
  (void) offset; // Unused in input

  // check the buffer and skip if it doesn't look like CSV text data.
  // (tud_msc_write10_cb is called multiple times per file save, 
  // but we only care about the call with the file content csv text data.)
  bool is_ascii = true; 
  bool has_comma = false;
  for (int i = 0; i < bufsize; i++) {
    if (buffer[i] > 127) {
      is_ascii = false; // non-ASCII character found
      break;
    }
    if (buffer[i] == ',') {
      has_comma = true; // comma found
    }
  }
  if (!is_ascii || !has_comma) {
    return (int32_t) bufsize;
  }

  // extract the data at the specified row/column and send it
  int row = 0; // current row
  int col = 0; // current column
  uint8_t* start = buffer; // start of current cell
  uint8_t* pos = buffer; // current position in string
  while (pos < buffer + bufsize && col <= COL &&!(row > ROW && col == COL)) {
    if (*pos == ',' || *pos == '\n' || *pos == '\0' || pos == buffer + bufsize - 1) {
      if (row == ROW && col == COL) {
        for (const uint8_t* p = start; p < pos; p++) {
          // printf("%c", *p);
          uart_putc_raw(uart1, *p); // -> this is where the cell data is sent to the other pico
        }
        // printf("\n");
        uart_putc_raw(uart1, '\n');
      }

      // end of cell, or end of sting
      if (*pos == ',' || *pos == '\0' || pos == buffer + bufsize - 1) {
        col++;
        start = pos + 1;
      }
      // end of row
      if (*pos == '\n') {
        row++;
        col = 0;
        start = pos + 1;
      }
    }
    pos++;
  }

  return (int32_t) bufsize;
}
```

## Device B (HID)

In the tinyUSB example named hid_multiple_interface, the microcontroller is configured as a basic keyboard and mouse (When the controller's button is pushed, it types the letter 'a' and moves the mouse).

For the LIT, the function hid_task in main.c has been edited, so that it reads characters recieved from microcontroller A, and sends keycodes to the PC.

```C
  /*------------- Enter data from UART -------------*/
  if ( tud_hid_n_ready(ITF_KEYBOARD) )
  {
    // use to avoid send multiple consecutive zero report for keyboard
    static bool has_key = false;

    if ( uart_is_readable(uart1) )
    {
      uint8_t keycode[6] = { 0 };
      char ch = uart_getc(uart1); // Read character from UART

      if (ch == '0') {
        keycode[0] = HID_KEY_0;
      } else if (ch == '1') {
        keycode[0] = HID_KEY_1;
      } else if (ch == '2') {
        keycode[0] = HID_KEY_2;
      } else if (ch == '3') {
        keycode[0] = HID_KEY_3;
      } else if (ch == '4') {
        keycode[0] = HID_KEY_4;
      } else if (ch == '5') {
        keycode[0] = HID_KEY_5;
      } else if (ch == '6') {
        keycode[0] = HID_KEY_6;
      } else if (ch == '7') {
        keycode[0] = HID_KEY_7;
      } else if (ch == '8') {
        keycode[0] = HID_KEY_8;
      } else if (ch == '9') {
        keycode[0] = HID_KEY_9;
      } else if (ch == '.') {
        keycode[0] = HID_KEY_PERIOD;
      }

      if (keycode[0] != 0 ) {
        tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, keycode);
        has_key = true;
      }
    } else
    {
      // send empty key report if previously has key pressed
      if (has_key) tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, NULL);
      has_key = false;
    }
  }

```

## Pin Connections

**power supply**

Both devices are powered by the PC's USB supply voltage. The red power line on device A's USB cable is cut, to disconnect it from the LI's USB supply voltage.

| controller A pin | controller B pin | description |
| --- | --- | --- |
| 39 | 40 | controller B's USB voltage from the PC (VBUS) is connected to controller A's input voltage (VSYS) |
| 38 | 38 | grounds are connected |
| 8 | 8 | grounds are connected |

**UART**

| controller A pin | controller B pin | description |
| --- | --- | --- |
| 6 | 7 | controller A's TX is connected to controller B's RX |
| 7 | 6 | controller A's RX is connected to controller B's TX (This is optional since com is simplex) |
