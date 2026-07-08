################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

CG_TOOL_ROOT := D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS

GEN_OPTS__FLAG := 
GEN_CMDS__FLAG := 

ORDERED_OBJS += \
"./adc.o" \
"./gpio.o" \
"./main.o" \
"./startup_ccs.o" \
"./state.o" \
"./uart.o" \
"./uart1.o" \
"../project0_ccs.cmd" \
$(GEN_CMDS__FLAG) \
-Wl,-l"C:/TI/TivaWare_C_Series-2.2.0.295/driverlib/ccs/Debug/driverlib.lib" \
-Wl,-llibc.a \
-Wl,-l"D:/ti/TivaWare_C_Series-2.2.0.295/driverlib/ccs/Debug/driverlib.lib" \

-include ../makefile.init

RM := DEL /F
RMDIR := RMDIR /S/Q

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir_vars.mk
-include subdir_rules.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C55_DEPS)),)
-include $(C55_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
ifneq ($(strip $(S67_DEPS)),)
-include $(S67_DEPS)
endif
ifneq ($(strip $(S62_DEPS)),)
-include $(S62_DEPS)
endif
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(OPT_DEPS)),)
-include $(OPT_DEPS)
endif
ifneq ($(strip $(C??_DEPS)),)
-include $(C??_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(S??_DEPS)),)
-include $(S??_DEPS)
endif
ifneq ($(strip $(C64_DEPS)),)
-include $(C64_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(S64_DEPS)),)
-include $(S64_DEPS)
endif
ifneq ($(strip $(CLA_DEPS)),)
-include $(CLA_DEPS)
endif
ifneq ($(strip $(S55_DEPS)),)
-include $(S55_DEPS)
endif
ifneq ($(strip $(SV7A_DEPS)),)
-include $(SV7A_DEPS)
endif
ifneq ($(strip $(C62_DEPS)),)
-include $(C62_DEPS)
endif
ifneq ($(strip $(C67_DEPS)),)
-include $(C67_DEPS)
endif
ifneq ($(strip $(K_DEPS)),)
-include $(K_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(C43_DEPS)),)
-include $(C43_DEPS)
endif
ifneq ($(strip $(S43_DEPS)),)
-include $(S43_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
ifneq ($(strip $(SA_DEPS)),)
-include $(SA_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
EXE_OUTPUTS += \
project0.out 

EXE_OUTPUTS__QUOTED += \
"project0.out" 


# All Target
all: $(OBJS) $(CMD_SRCS) $(GEN_CMDS)
	@$(MAKE) --no-print-directory -Onone "project0.out"

# Tool invocations
project0.out: $(OBJS) $(CMD_SRCS) $(GEN_CMDS)
	@echo 'Arm Linker - building target: "$@"'
	"D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/bin/tiarmclang.exe" -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mlittle-endian -mthumb -DPART_TM4C123GH6PM -Wall -fdata-sections -ffunction-sections -Wl,-m"project0_ccs.map" -Wl,--stack_size=256 -Wl,-i"D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/lib" -Wl,-i"D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/include" -Wl,--reread_libs -Wl,--diag_wrap=off -Wl,--display_error_number -Wl,--warn_sections -Wl,--xml_link_info="project0_linkInfo.xml" -Wl,--rom_model -o "project0.out" $(ORDERED_OBJS)
	@echo 'Finished building target: "$@"'
	@echo ' '
	@$(MAKE) --no-print-directory post-build

# Other Targets
clean:
	-$(RM) $(EXE_OUTPUTS__QUOTED)
	-$(RM) "adc.o" "gpio.o" "main.o" "startup_ccs.o" "state.o" "uart.o" "uart1.o" 
	-$(RM) "adc.d" "gpio.d" "main.d" "startup_ccs.d" "state.d" "uart.d" "uart1.d" 
	-@echo ' '

post-build:
	-"D:/ti/ccs2100/ccs//utils/tiobj2bin/tiobj2bin" "project0.out" "project0.bin" "D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/bin/armofd" "D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/bin/armhex" "D:/ti/ccs2100/ccs//utils/tiobj2bin/mkhex4bin"
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets

