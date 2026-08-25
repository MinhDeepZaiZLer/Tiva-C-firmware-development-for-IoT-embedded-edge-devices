#ifndef BOOT_FLASH_H_
#define BOOT_FLASH_H_

#include <stdint.h>

#define BOOT_APP_BASE 0x00004000u
#define BOOT_FLASH_END 0x00040000u
#define BOOT_FLASH_PAGE_SIZE 1024u

int Boot_FlashEraseApplication(uint32_t image_size);
int Boot_FlashWrite(uint32_t address, const uint8_t *data, uint32_t length);
uint32_t Boot_Crc32(const uint8_t *data, uint32_t length, uint32_t crc);

#endif
