################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/Library/src/my_keypad.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/Library/src/my_led.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/Library/src/my_uart3.c 

OBJS += \
./Library/src/my_keypad.o \
./Library/src/my_led.o \
./Library/src/my_uart3.o 

C_DEPS += \
./Library/src/my_keypad.d \
./Library/src/my_led.d \
./Library/src/my_uart3.d 


# Each subdirectory must supply rules for building sources it contributes
Library/src/my_keypad.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/Library/src/my_keypad.c Library/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad/../Library/inc" -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad/../Library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Library/src/my_led.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/Library/src/my_led.c Library/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad/../Library/inc" -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad/../Library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Library/src/my_uart3.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/Library/src/my_uart3.c Library/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad/../Library/inc" -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad/../Library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Library-2f-src

clean-Library-2f-src:
	-$(RM) ./Library/src/my_keypad.cyclo ./Library/src/my_keypad.d ./Library/src/my_keypad.o ./Library/src/my_keypad.su ./Library/src/my_led.cyclo ./Library/src/my_led.d ./Library/src/my_led.o ./Library/src/my_led.su ./Library/src/my_uart3.cyclo ./Library/src/my_uart3.d ./Library/src/my_uart3.o ./Library/src/my_uart3.su

.PHONY: clean-Library-2f-src

