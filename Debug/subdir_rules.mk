################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/bin/tiarmclang.exe" -c -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mlittle-endian -mthumb -I"C:/ti/TivaWare_C_Series-2.2.0.295" -I"C:/ti/TivaWare_C_Series-2.2.0.295/inc" -DPART_TM4C123GH6PM -Wall -fdata-sections -ffunction-sections -MMD -MP -MF"$(basename $(<F)).d_raw" -MT"$(@)"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

main.o: ../main.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"D:/ti/ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS/bin/tiarmclang.exe" -c -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mlittle-endian -mthumb -I"D:/ti/TivaWare_C_Series-2.2.0.295/driverlib" -I"D:/ti/TivaWare_C_Series-2.2.0.295" -I"D:/ti/TivaWare_C_Series-2.2.0.295/inc" -DPART_TM4C123GH6PM -Wall -fdata-sections -ffunction-sections -MMD -MP -MF"$(basename $(<F)).d_raw" -MT"$(basename\ $(<F)).o"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


