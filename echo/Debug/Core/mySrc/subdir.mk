################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/uart3_protocol.c 

OBJS += \
./Core/mySrc/uart3_protocol.o 

C_DEPS += \
./Core/mySrc/uart3_protocol.d 


# Each subdirectory must supply rules for building sources it contributes
Core/mySrc/uart3_protocol.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/uart3_protocol.c Core/mySrc/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../library -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-mySrc

clean-Core-2f-mySrc:
	-$(RM) ./Core/mySrc/uart3_protocol.cyclo ./Core/mySrc/uart3_protocol.d ./Core/mySrc/uart3_protocol.o ./Core/mySrc/uart3_protocol.su

.PHONY: clean-Core-2f-mySrc

