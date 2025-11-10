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

#include "bsp/board_api.h"
#include "tusb.h"
#include "hardware/uart.h"
#include "disk.h"

// Whether host does safe-eject
static bool ejected = false;

// Invoked when received SCSI_CMD_INQUIRY
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void)lun;

  printf("### SCSI INQUIRY ###\r\n");

  const char vid[] = "TinyUSB";
  const char pid[] = "Mass Storage";
  const char rev[] = "1.0";
  memcpy(vendor_id, vid, strlen(vid));
  memcpy(product_id, pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  (void)lun;

  printf("### TEST UNIT READY ###\r\n");

  if (ejected)
  {
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return false;
  }
  return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
  (void)lun;

  printf("### SCSI READ CAPACITY ###\r\n");

  *block_count = DISK_BLOCK_NUM - 1; // Last LBA
  *block_size = DISK_BLOCK_SIZE;
}

// Invoked when received Start Stop Unit command
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
  (void)lun;
  (void)power_condition;

  printf("### START STOP UNIT ###\r\n");

  if (load_eject)
  {
    if (!start)
    {
      ejected = true;
    }
  }
  return true;
}

// Callback for unhandled SCSI commands
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
  printf("### UNHANDLED SCSI COMMAND: 0x%02X ###\r\n", scsi_cmd[0]);
}

  // Callback for READ10 command
  int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
  {
    (void)lun;

    // Debug: Log block reads to UART
    printf("### READ: LBA=%lu ###\r\n", lba);

    if (offset != 0)
    {
      printf("### OFFSET=%lu ###\r\n", offset);
    }

    if (bufsize != DISK_BLOCK_SIZE)
    {
      printf("### BUFSIZE=%lu ###\r\n", bufsize);
    }

    if (lba == 0)
    {
      memcpy(buffer, lba_0, DISK_BLOCK_SIZE);
    }
    else if (lba == 4)
    {
      memcpy(buffer, lba_4, DISK_BLOCK_SIZE);
    }
    else if (lba == 68)
    {
      memcpy(buffer, lba_68, DISK_BLOCK_SIZE);
    }
    else if (lba == 132)
    {
      memcpy(buffer, lba_132, DISK_BLOCK_SIZE);
    }
    else if (lba == 168)
    {
      memcpy(buffer, lba_168, DISK_BLOCK_SIZE);
    }
    else // lba_1 is all zeros
    {
      memcpy(buffer, lba_1, DISK_BLOCK_SIZE);
    }

    return (int32_t)bufsize;
  }

  bool tud_msc_is_writable_cb(uint8_t lun)
  {
    (void)lun;

    printf("### IS WRITABLE ###\r\n");

    return true;
  }

  // Callback for WRITE10 command
  const int ROW = 5;
  const int COL = 2;
  int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
  {
    (void)lun;
    (void)offset;

    char msg[64];

    // Debug: Log block writes to UART
    printf("### WRITE: LBA=%lu ###\r\n", lba);


    if (offset != 0)
    {
      printf(msg, "### OFFSET=%lu ###\r\n", offset);
    }

    if (bufsize != DISK_BLOCK_SIZE)
    {
      printf("### BUFSIZE=%lu ###\r\n", bufsize);
    }

    // Process ASCII CSV data for UART
    bool has_comma = false;
    for (int i = 0; i < bufsize; i++)
    {
      if (buffer[i] == ',')
      {
        has_comma = true;
      }
    }
    if (!has_comma)
    {
      return (int32_t)bufsize;
    }
    int row = 0;
    int col = 0;
    uint8_t *start = buffer;
    uint8_t *pos = buffer;
    while (pos < buffer + bufsize && row <= ROW)
    {
      if (*pos == ',' || *pos == '\n' || *pos == '\0' || pos == buffer + bufsize - 1)
      {
        if (row == ROW && col == COL)
        {
          printf("### DATA=");
          for (const uint8_t *p = start; p < pos; p++)
          {
            uart_putc_raw(uart1, *p);
            printf("%c", *p);
          }
          uart_putc_raw(uart1, '\n');
          printf(" ###\r\n");
        }
        if (*pos == ',' || *pos == '\0' || pos == buffer + bufsize - 1)
        {
          col++;
          start = pos + 1;
        }
        if (*pos == '\n')
        {
          row++;
          col = 0;
          start = pos + 1;
        }
      }
      pos++;
    }
    return (int32_t)bufsize;
  }
