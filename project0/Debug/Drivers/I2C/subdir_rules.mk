################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
Drivers/I2C/%.o: ../Drivers/I2C/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/bin/tiarmclang.exe" -c -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mlittle-endian -mthumb -I"C:/ti/TivaWare_C_Series-2.2.0.295" -I"C:/ti/TivaWare_C_Series-2.2.0.295/inc" -I"D:/CCStudio_Workspace/project0/Application" -I"D:/CCStudio_Workspace/project0/Common" -I"D:/CCStudio_Workspace/project0/Devices/MMA7660" -I"D:/CCStudio_Workspace/project0/Devices/AM2301B" -I"D:/CCStudio_Workspace/project0/Devices/LCD" -I"D:/CCStudio_Workspace/project0/Drivers/GPIO" -I"D:/CCStudio_Workspace/project0/Drivers/ADC" -I"D:/CCStudio_Workspace/project0/Drivers/I2C" -I"D:/CCStudio_Workspace/project0/Drivers/UART" -DPART_TM4C123GH6PM -Wall -fdata-sections -ffunction-sections -MMD -MP -MF"Drivers/I2C/$(basename $(<F)).d_raw" -MT"$(@)"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


