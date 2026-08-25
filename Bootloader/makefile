CC = D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/bin/tiarmclang.exe
OBJCOPY = D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/bin/tiarmobjcopy.exe
LIB = D:/ti/TivaWare_C_Series-2.2.0.295/driverlib/ccs/Debug/driverlib.lib
INCLUDES = -IC:/ti/TivaWare_C_Series-2.2.0.295 -IC:/ti/TivaWare_C_Series-2.2.0.295/inc -IC:/ti/TivaWare_C_Series-2.2.0.295/driverlib -ID:/ti/TivaWare_C_Series-2.2.0.295 -ID:/ti/TivaWare_C_Series-2.2.0.295/inc
CPU = -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mlittle-endian -mthumb
CFLAGS = $(CPU) $(INCLUDES) -DPART_TM4C123GH6PM -Wall -ffunction-sections -fdata-sections
LDFLAGS = $(CPU) -Wl,-mbootloader.map -Wl,--stack_size=2048 -Wl,-iD:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/lib -Wl,-iD:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/include -Wl,--reread_libs -Wl,--diag_wrap=off -Wl,--display_error_number -Wl,--warn_sections -Wl,--rom_model

all: bootloader.bin

bootloader.bin: bootloader.out
	"$(OBJCOPY)" -O binary "$<" "$@"

bootloader.out: bootloader_startup.o bootloader.o boot_flash.o bootloader_ccs.cmd
	"$(CC)" $(LDFLAGS) -o "$@" bootloader_startup.o bootloader.o boot_flash.o bootloader_ccs.cmd -Wl,-llibc.a -Wl,-l"$(LIB)"

bootloader_startup.o: bootloader_startup.c
	"$(CC)" -c $(CFLAGS) -o "$@" "$<"

bootloader.o: bootloader.c
	"$(CC)" -c $(CFLAGS) -o "$@" "$<"

boot_flash.o: boot_flash.c boot_flash.h
	"$(CC)" -c $(CFLAGS) -o "$@" "$<"

clean:
	-DEL /F /Q bootloader.out bootloader.bin bootloader.o bootloader_startup.o boot_flash.o bootloader.map

.PHONY: all clean
