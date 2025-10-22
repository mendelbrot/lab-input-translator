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

#if CFG_TUD_MSC

// Whether host does safe-eject
static bool ejected = false;

#define DISK_BLOCK_NUM 128  // 64 KB total disk size
#define DISK_BLOCK_SIZE 512 // Standard block size

// Disk layout:
// Block 0: MBR with one FAT32 partition (starts at sector 8, 120 sectors)
// Blocks 1-7: Reserved
// Block 8: FAT32 Boot Sector
// Block 9: FSInfo Sector
// Blocks 10-13: Reserved
// Block 14: Backup Boot Sector
// Block 15: Backup FSInfo Sector
// Blocks 16-19: FAT1
// Blocks 20-23: FAT2
// Block 24: Root Directory (cluster 2)
// Block 25: Logger Directory (cluster 3)
// Blocks 26-127: Data region

#ifdef CFG_EXAMPLE_MSC_READONLY
const
#endif
    uint8_t msc_disk[DISK_BLOCK_NUM][DISK_BLOCK_SIZE] =
        {
            //------------- Block 0: MBR Partition Table -------------//
            {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                // Padding (446 bytes of zeros until partition table)
                [446] = 0x00,           // Boot indicator (non-bootable)
                0x00, 0x02, 0x00,       // Starting CHS (simplified)
                0x0C,                   // Partition type (FAT32 LBA)
                0x3F, 0xFF, 0xFF,       // Ending CHS (simplified)
                0x08, 0x00, 0x00, 0x00, // Starting LBA (sector 8)
                0x78, 0x00, 0x00, 0x00, // Partition size (120 sectors)
                                        // Remaining partition entries (all zeros)
                [462] = 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                [510] = 0x55, 0xAA // MBR signature
            },

            //------------- Blocks 1-7: Reserved -------------//
            [1 ... 7] = {/* All zeros */},

            //------------- Block 8: FAT32 Boot Sector -------------//
            {
                0xEB, 0x58, 0x90,                                                       // Jump instruction
                'M', 'S', 'D', 'O', 'S', '5', '.', '0',                                 // OEM Name
                0x00, 0x02,                                                             // Bytes per sector (512)
                0x01,                                                                   // Sectors per cluster (1)
                0x08, 0x00,                                                             // Reserved sectors (8)
                0x02,                                                                   // Number of FATs
                0x00, 0x00,                                                             // Root entry count (0 for FAT32)
                0x00, 0x00,                                                             // Total sectors (small, 0 for FAT32)
                0xF8,                                                                   // Media descriptor
                0x00, 0x00,                                                             // Sectors per FAT (16-bit, 0 for FAT32)
                0x00, 0x00,                                                             // Sectors per track (0)
                0x00, 0x00,                                                             // Number of heads (0)
                0x08, 0x00, 0x00, 0x00,                                                 // Hidden sectors (8)
                0x78, 0x00, 0x00, 0x00,                                                 // Total sectors (120)
                0x04, 0x00, 0x00, 0x00,                                                 // Sectors per FAT (32-bit, 4 sectors)
                0x00, 0x00,                                                             // Flags
                0x00, 0x00,                                                             // FAT version
                0x02, 0x00, 0x00, 0x00,                                                 // Root directory cluster (2)
                0x01, 0x00,                                                             // FSInfo sector (relative to partition, 1 = Block 9)
                0x06, 0x00,                                                             // Backup boot sector (relative, 6 = Block 14)
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reserved
                0x00,                                                                   // Drive number (removable)
                0x00,                                                                   // Reserved
                0x29,                                                                   // Extended boot signature
                0x34, 0x12, 0x56, 0x78,                                                 // Volume serial number (unique)
                'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' ',                  // Volume label (NO NAME)
                'F', 'A', 'T', '3', '2', ' ', ' ', ' ',                                 // Filesystem type
                                                                                        // Boot code (zeros)
                [90] = 0x00,                                                            // Fill remaining with zeros
                [510] = 0x55, 0xAA                                                      // Boot sector signature
            },

            //------------- Block 9: FSInfo Sector -------------//
            {
                0x52, 0x52, 0x61, 0x41, // Lead signature
                                        // Reserved (480 bytes)
                [4] = 0x00, [483] = 0x00,
                0x72, 0x72, 0x41, 0x61, // Structure signature
                0xFF, 0xFF, 0xFF, 0xFF, // Free cluster count (unknown)
                0x04, 0x00, 0x00, 0x00, // Next free cluster (4)
                                        // Reserved
                [496] = 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x55, 0xAA // Trailing signature
            },

            //------------- Blocks 10-13: Reserved -------------//
            [10 ... 13] = {/* All zeros */},

            //------------- Block 14: Backup Boot Sector -------------//
            {
                0xEB, 0x58, 0x90,                                                       // Jump instruction
                'M', 'S', 'D', 'O', 'S', '5', '.', '0',                                 // OEM Name
                0x00, 0x02,                                                             // Bytes per sector (512)
                0x01,                                                                   // Sectors per cluster (1)
                0x08, 0x00,                                                             // Reserved sectors (8)
                0x02,                                                                   // Number of FATs
                0x00, 0x00,                                                             // Root entry count (0 for FAT32)
                0x00, 0x00,                                                             // Total sectors (small, 0 for FAT32)
                0xF8,                                                                   // Media descriptor
                0x00, 0x00,                                                             // Sectors per FAT (16-bit, 0 for FAT32)
                0x00, 0x00,                                                             // Sectors per track (0)
                0x00, 0x00,                                                             // Number of heads (0)
                0x08, 0x00, 0x00, 0x00,                                                 // Hidden sectors (8)
                0x78, 0x00, 0x00, 0x00,                                                 // Total sectors (120)
                0x04, 0x00, 0x00, 0x00,                                                 // Sectors per FAT (32-bit, 4 sectors)
                0x00, 0x00,                                                             // Flags
                0x00, 0x00,                                                             // FAT version
                0x02, 0x00, 0x00, 0x00,                                                 // Root directory cluster (2)
                0x01, 0x00,                                                             // FSInfo sector (relative to partition, 1 = Block 9)
                0x06, 0x00,                                                             // Backup boot sector (relative, 6 = Block 14)
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Reserved
                0x00,                                                                   // Drive number (removable)
                0x00,                                                                   // Reserved
                0x29,                                                                   // Extended boot signature
                0x34, 0x12, 0x56, 0x78,                                                 // Volume serial number (unique)
                'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' ',                  // Volume label (NO NAME)
                'F', 'A', 'T', '3', '2', ' ', ' ', ' ',                                 // Filesystem type
                                                                                        // Boot code (zeros)
                [90] = 0x00,                                                            // Fill remaining with zeros
                [510] = 0x55, 0xAA                                                      // Boot sector signature
            },

            //------------- Block 15: Backup FSInfo Sector -------------//
            {
                0x52, 0x52, 0x61, 0x41, // Lead signature
                                        // Reserved (480 bytes)
                [4] = 0x00, [483] = 0x00,
                0x72, 0x72, 0x41, 0x61, // Structure signature
                0xFF, 0xFF, 0xFF, 0xFF, // Free cluster count (unknown)
                0x04, 0x00, 0x00, 0x00, // Next free cluster (4)
                                        // Reserved
                [496] = 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x55, 0xAA // Trailing signature
            },

            //------------- Blocks 16-19: FAT1 -------------//
            {
                0xF8, 0xFF, 0xFF, 0x0F, // Media descriptor, reserved
                0xFF, 0xFF, 0xFF, 0x0F, // Cluster 1 (reserved)
                0xFF, 0xFF, 0xFF, 0x0F, // Cluster 2 (root dir, EOC)
                0xFF, 0xFF, 0xFF, 0x0F, // Cluster 3 (logger dir, EOC)
                                        // Remaining zeros
            },
            [17 ... 19] = {/* All zeros */},

            //------------- Blocks 20-23: FAT2 (Backup) -------------//
            {
                0xF8, 0xFF, 0xFF, 0x0F, // Media descriptor, reserved
                0xFF, 0xFF, 0xFF, 0x0F, // Cluster 1 (reserved)
                0xFF, 0xFF, 0xFF, 0x0F, // Cluster 2 (root dir, EOC)
                0xFF, 0xFF, 0xFF, 0x0F, // Cluster 3 (logger dir, EOC)
                                        // Remaining zeros
            },
            [21 ... 23] = {/* All zeros */},

            //------------- Block 24: Root Directory (Cluster 2) -------------//
            {
                // Volume label entry
                'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' ', // Name
                0x08,                                                  // Volume label attribute
                0x00,                                                  // Reserved
                0x00,                                                  // Creation time (tenths)
                0x00, 0x34,                                            // Creation time (10:00:00)
                0x2A, 0x42,                                            // Creation date (2023-01-01)
                0x2A, 0x42,                                            // Last access date
                0x00, 0x00,                                            // First cluster (high, 0)
                0x00, 0x34,                                            // Last mod time
                0x2A, 0x42,                                            // Last mod date
                0x00, 0x00,                                            // First cluster (low, 0)
                0x00, 0x00, 0x00, 0x00,                                // File size (0)
                                                                       // Logger folder entry
                'L', 'O', 'G', 'G', 'E', 'R', ' ', ' ', ' ', ' ', ' ', // Name (lowercase)
                0x10,                                                  // Directory attribute
                0x18,                                                  // Reserved (NT byte for lowercase basename)
                0x00,                                                  // Creation time (tenths)
                0x00, 0x34,                                            // Creation time (10:00:00)
                0x2A, 0x42,                                            // Creation date (2023-01-01)
                0x2A, 0x42,                                            // Last access date
                0x00, 0x00,                                            // First cluster (high)
                0x00, 0x34,                                            // Last mod time
                0x2A, 0x42,                                            // Last mod date
                0x03, 0x00,                                            // First cluster (low, cluster 3)
                0x00, 0x00, 0x00, 0x00,                                // File size (0)
                                                                       // Remaining zeros
            },

            //------------- Block 25: Logger Directory (Cluster 3) -------------//
            {
                // . entry
                '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', // Name
                0x10,                                                  // Directory attribute
                0x00,                                                  // Reserved
                0x00,                                                  // Creation time (tenths)
                0x00, 0x34,                                            // Creation time (10:00:00)
                0x2A, 0x42,                                            // Creation date (2023-01-01)
                0x2A, 0x42,                                            // Last access date
                0x00, 0x00,                                            // First cluster (high)
                0x00, 0x34,                                            // Last mod time
                0x2A, 0x42,                                            // Last mod date
                0x03, 0x00,                                            // First cluster (low, self: cluster 3)
                0x00, 0x00, 0x00, 0x00,                                // File size
                                                                       // .. entry
                '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', // Name
                0x10,                                                  // Directory attribute
                0x00,                                                  // Reserved
                0x00,                                                  // Creation time (tenths)
                0x00, 0x34,                                            // Creation time (10:00:00)
                0x2A, 0x42,                                            // Creation date (2023-01-01)
                0x2A, 0x42,                                            // Last access date
                0x00, 0x00,                                            // First cluster (high)
                0x00, 0x34,                                            // Last mod time
                0x2A, 0x42,                                            // Last mod date
                0x02, 0x00,                                            // First cluster (low, parent: root at cluster 2)
                0x00, 0x00, 0x00, 0x00,                                // File size
                                                                       // Remaining zeros
            },

            //------------- Blocks 26-127: Data Region -------------//
            [26 ... 127] = {/* All zeros */}};

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

  *block_count = DISK_BLOCK_NUM;
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
  if (lba >= DISK_BLOCK_NUM)
    return -1;
  // Debug: Log block reads to UART
  
  char msg[32];
  sprintf(msg, "### READ10: LBA=%lu, Size=%lu ###\r\n", lba, bufsize);
  printf(msg);
  uint8_t const *addr = msc_disk[lba] + offset;
  memcpy(buffer, addr, bufsize);
  return (int32_t)bufsize;
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

  // Allow writes to data region (Blocks 26-127) for persistence
  // if (lba >= 26 && lba < DISK_BLOCK_NUM)
  // {
  //   memcpy(msc_disk[lba], buffer, bufsize);
  // }
  // Process ASCII CSV data for UART
  bool is_ascii = true;
  bool has_comma = false;
  for (int i = 0; i < bufsize; i++)
  {
    // if (buffer[i] > 127)
    // {
    //   is_ascii = false;
    //   break;
    // }
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
      {
        for (const uint8_t *p = start; p < pos; p++)
        {
          uart_putc_raw(uart1, *p);
        }
        uart_putc_raw(uart1, '\n');
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

  printf("### unhandled SCSI commands ###\r\n");

  void const *response = NULL;
  int32_t resplen = 0;
  bool in_xfer = true;
  switch (scsi_cmd[0])
  {
  default:
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    resplen = -1;
    break;
  }
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