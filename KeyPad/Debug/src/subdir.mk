################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/Users/shihyenchieh/Downloads/Graduation\ project/code/library/src/my_keypad.c \
/Users/shihyenchieh/Downloads/Graduation\ project/code/library/src/my_led.c \
/Users/shihyenchieh/Downloads/Graduation\ project/code/library/src/my_uart3.c 

OBJS += \
./src/my_keypad.o \
./src/my_led.o \
./src/my_uart3.o 

C_DEPS += \
./src/my_keypad.d \
./src/my_led.d \
./src/my_uart3.d 


# Each subdirectory must supply rules for building sources it contributes
src/my_keypad.o: /Users/shihyenchieh/Downloads/Graduation\ project/code/library/src/my_keypad.c src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/Graduation project/code/KeyPad/../library/inc" -I"/Users/shihyenchieh/Downloads/Graduation project/code/KeyPad/../library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
src/my_led.o: /Users/shihyenchieh/Downloads/Graduation\ project/code/library/src/my_led.c src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/Graduation project/code/KeyPad/../library/inc" -I"/Users/shihyenchieh/Downloads/Graduation project/code/KeyPad/../library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
src/my_uart3.o: /Users/shihyenchieh/Downloads/Graduation\ project/code/library/src/my_uart3.c src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/Graduation project/code/KeyPad/../library/inc" -I"/Users/shihyenchieh/Downloads/Graduation project/code/KeyPad/../library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-src

clean-src:
	-$(RM) ./src/my_keypad.cyclo ./src/my_keypad.d ./src/my_keypad.o ./src/my_keypad.su ./src/my_led.cyclo ./src/my_led.d ./src/my_led.o ./src/my_led.su ./src/my_uart3.cyclo ./src/my_uart3.d ./src/my_uart3.o ./src/my_uart3.su

.PHONY: clean-src

