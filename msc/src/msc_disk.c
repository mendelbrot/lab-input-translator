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

#if CFG_TUD_MSC

// Whether host does safe-eject
static bool ejected = false;

// Invoked when received SCSI_CMD_INQUIRY
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void)lun;

  printf("### SCSI_CMD_INQUIRY ###\r\n");

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

  printf("### Test Unit Ready ###\r\n");

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

  printf("### SCSI_CMD_READ_CAPACITY_10 ###\r\n");

  *block_count = DISK_BLOCK_NUM - 1; // Last LBA
  *block_size = DISK_BLOCK_SIZE;
}

// Invoked when received Start Stop Unit command
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
  (void)lun;
  (void)power_condition;

  printf("### Start Stop Unit ###\r\n");

  if (load_eject)
  {
    if (!start)
    {
      ejected = true;
    }
  }
  return true;
}

// Callback for READ10 command
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
  (void)lun;

  // Debug: Log block reads to UART
  char msg[32];
  sprintf(msg, "### READ10: LBA=%lu, OFFSET=%lu, Size=%lu ###\r\n", lba, offset, bufsize);
  printf(msg);

  // uint8_t const *addr = msc_disk[lba] + offset;
  // memcpy(buffer, addr, bufsize);
  // return (int32_t)bufsize;

  if (offset != 0) {
    printf("### READ10: UNHANDLED OFFSET ###");
  }

  if (bufsize != DISK_BLOCK_SIZE)
  {
    printf("### READ10: UNHANDLED BUFFER SIZE ###");
  }

  if (lba == 0)
  {
    memcpy(buffer, lba_0, DISK_BLOCK_SIZE);

    // Hex dump buffer for debug (first 512 bytes)
    uint8_t *buf8 = (uint8_t *)buffer;
    for (int i = 0; i < 512; i++)
    {
      if (i % 16 == 0)
        printf("\n%04x: ", i);
      printf("%02x ", buf8[i]);
      if (i % 16 == 15)
      {
        printf(" |");
        for (int j = 0; j < 16; j++)
        {
          char c = buf8[i - 15 + j];
          printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
        printf("|");
      }
    }
    printf("\n");

    return (int32_t)bufsize;
  } 
  else if (lba == 4) 
  {
    memcpy(buffer, lba_4, DISK_BLOCK_SIZE);
    return (int32_t)bufsize;
  }
  else if (lba == 68)
  {
    memcpy(buffer, lba_68, DISK_BLOCK_SIZE);
    return (int32_t)bufsize;
  }
  else if (lba == 132)
  {
    memcpy(buffer, lba_132, DISK_BLOCK_SIZE);
    return (int32_t)bufsize;
  }
  else if (lba == 168)
  {
    memcpy(buffer, lba_168, DISK_BLOCK_SIZE);
    return (int32_t)bufsize;
  }
  else // lba_1 is all zeros
  {
    printf("### READ10: UNHANDLED SECTOR ###");
    memcpy(buffer, lba_1, DISK_BLOCK_SIZE);
    return (int32_t)bufsize;
  }
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
  (void)lun;

  printf("### Is Writable ###\r\n");

  return true;
}

// Callback for WRITE10 command
const int ROW = 5;
const int COL = 2;
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
  (void)lun;
  (void)offset;
  // Debug: Log block writes to UART
  char msg[32];
  sprintf(msg, "### WRITE10: LBA=%lu, Size=%lu ###\r\n", lba, bufsize);
  printf(msg);

  // Process ASCII CSV data for UART
  bool is_ascii = true;
  bool has_comma = false;
  for (int i = 0; i < bufsize; i++)
  {
    if (buffer[i] > 127)
    {
      is_ascii = false;
      break;
    }
    if (buffer[i] == ',')
    {
      has_comma = true;
    }
  }
  if (!is_ascii || !has_comma)
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
      printf("### WRITE10: DATA: ");
      {
        for (const uint8_t *p = start; p < pos; p++)
        {
          uart_putc_raw(uart1, *p);
          printf("%c\n", *p);
        }
        uart_putc_raw(uart1, '\n');
        printf("%c\n", '\n');
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

// Callback for unhandled SCSI commands
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
  printf("### SCSI command: 0x%02X ###\r\n", scsi_cmd[0]);

  void const *response = NULL;
  int32_t resplen = 0;
  bool in_xfer = true;

  switch (scsi_cmd[0])
  {
  case 0x1A: // MODE SENSE(6)
  {
    uint8_t len = scsi_cmd[4];
    uint8_t mode_data[4] = {3, 0, 0, 0}; // Mode data length 3 (excluding header), medium 0, params 0 (WP=0), block desc len 0
    resplen = TU_MIN(len, sizeof(mode_data));
    response = mode_data;
    break;
  }

  case 0x23: // READ FORMAT CAPACITIES
  {
    uint16_t alen = (scsi_cmd[7] << 8) | scsi_cmd[8];
    uint8_t cap_list[12];
    // Header
    memset(cap_list, 0, 4);
    cap_list[3] = 8; // Capacity list length for 1 descriptor
    // Descriptor: max capacity (full media)
    cap_list[4] = (DISK_BLOCK_NUM >> 24) & 0xFF;
    cap_list[5] = (DISK_BLOCK_NUM >> 16) & 0xFF;
    cap_list[6] = (DISK_BLOCK_NUM >> 8) & 0xFF;
    cap_list[7] = DISK_BLOCK_NUM & 0xFF;
    cap_list[8] = 0x02;                           // Formatted media
    cap_list[9] = 0x00;                           // Reserved
    cap_list[10] = (DISK_BLOCK_SIZE >> 8) & 0xFF; // Block length BE16 high
    cap_list[11] = DISK_BLOCK_SIZE & 0xFF;        // low
    resplen = TU_MIN(alen, sizeof(cap_list));
    response = cap_list;
    break;
  }

  case 0x1E: // PREVENT ALLOW MEDIUM REMOVAL
  {
    // Ignore for thumb drive simulation (no lock)
    resplen = 0;
    break;
  }

  default:
    printf("### unhandled SCSI command ###\r\n");
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    resplen = -1;
    break;
  }

  if (resplen < 0)
    return resplen;
  if (resplen > bufsize)
    resplen = bufsize;
  if (response && resplen > 0)
  {
    if (in_xfer)
    {
      memcpy(buffer, response, (size_t)resplen);
    }
  }
  return (int32_t)resplen;
}

#endif