--retain=boot_vectors

#define BOOT_BASE 0x00000000
#define BOOT_SIZE 0x00004000
#define RAM_BASE 0x20000000
#define STACK_SIZE 2048

MEMORY
{
    FLASH (RX) : origin = BOOT_BASE, length = BOOT_SIZE
    SRAM (RWX) : origin = RAM_BASE, length = 0x00008000
}

SECTIONS
{
    .intvecs : > BOOT_BASE
    .text : > FLASH
    .const : > FLASH
    .cinit : > FLASH
    .pinit : > FLASH
    .init_array : > FLASH
    .data : > SRAM
    .bss : > SRAM
    .sysmem : > SRAM
    .stack : > SRAM
}

__STACK_TOP = __stack + STACK_SIZE;
