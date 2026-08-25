#include <stdbool.h>
#include <stdint.h>

#include "boot_flash.h"
#include "driverlib/flash.h"

static int Boot_FlashRangeValid(uint32_t address, uint32_t length) {
  uint32_t end;

  if ((length == 0u) || (address < BOOT_APP_BASE) ||
      (address >= BOOT_FLASH_END)) {
    return 0;
  }
  end = address + length;
  return (end >= address) && (end <= BOOT_FLASH_END);
}

int Boot_FlashEraseApplication(uint32_t image_size) {
  uint32_t address;
  uint32_t erase_size;

  if (!Boot_FlashRangeValid(BOOT_APP_BASE, image_size)) {
    return 0;
  }
  erase_size = (image_size + (BOOT_FLASH_PAGE_SIZE - 1u)) &
               ~(BOOT_FLASH_PAGE_SIZE - 1u);
  if (erase_size > (BOOT_FLASH_END - BOOT_APP_BASE)) {
    return 0;
  }

  for (address = BOOT_APP_BASE; address < (BOOT_APP_BASE + erase_size);
       address += BOOT_FLASH_PAGE_SIZE) {
    if (FlashErase(address) != 0) {
      return 0;
    }
  }
  return 1;
}

int Boot_FlashWrite(uint32_t address, const uint8_t *data, uint32_t length) {
  uint32_t word;
  uint32_t offset;
  uint32_t chunk;

  if ((data == 0) || !Boot_FlashRangeValid(address, length) ||
      ((address & 3u) != 0u)) {
    return 0;
  }

  for (offset = 0u; offset < length; offset += 4u) {
    word = 0xFFFFFFFFu;
    chunk = length - offset;
    if (chunk > 4u) {
      chunk = 4u;
    }
    for (uint32_t byte = 0u; byte < chunk; byte++) {
      word &= ~(0xFFu << (byte * 8u));
      word |= (uint32_t)data[offset + byte] << (byte * 8u);
    }
    FlashProgram(&word, address + offset, sizeof(word));
  }
  return 1;
}

uint32_t Boot_Crc32(const uint8_t *data, uint32_t length, uint32_t crc) {
  if (data == 0) {
    return crc;
  }
  while (length-- != 0u) {
    crc ^= *data++;
    for (uint32_t bit = 0u; bit < 8u; bit++) {
      crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return crc;
}
