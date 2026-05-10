################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/src/my_keypad.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/src/my_keypad_v2.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/src/my_led.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/src/my_uart3.c 

OBJS += \
./myLib/my_keypad.o \
./myLib/my_keypad_v2.o \
./myLib/my_led.o \
./myLib/my_uart3.o 

C_DEPS += \
./myLib/my_keypad.d \
./myLib/my_keypad_v2.d \
./myLib/my_led.d \
./myLib/my_uart3.d 


# Each subdirectory must supply rules for building sources it contributes
myLib/my_keypad.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/src/my_keypad.c myLib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad_Interrupt/../Library/inc" -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad_Interrupt/../Library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
myLib/my_keypad_v2.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/src/my_keypad_v2.c myLib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad_Interrupt/../Library/inc" -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad_Interrupt/../Library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
myLib/my_led.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/src/my_led.c myLib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad_Interrupt/../Library/inc" -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad_Interrupt/../Library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
myLib/my_uart3.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/src/my_uart3.c myLib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad_Interrupt/../Library/inc" -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/KeyPad_Interrupt/../Library/src" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-myLib

clean-myLib:
	-$(RM) ./myLib/my_keypad.cyclo ./myLib/my_keypad.d ./myLib/my_keypad.o ./myLib/my_keypad.su ./myLib/my_keypad_v2.cyclo ./myLib/my_keypad_v2.d ./myLib/my_keypad_v2.o ./myLib/my_keypad_v2.su ./myLib/my_led.cyclo ./myLib/my_led.d ./myLib/my_led.o ./myLib/my_led.su ./myLib/my_uart3.cyclo ./myLib/my_uart3.d ./myLib/my_uart3.o ./myLib/my_uart3.su

.PHONY: clean-myLib

