################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/Library/not\ use/src/my_keypad.c 

OBJS += \
./Library/not\ use/src/my_keypad.o 

C_DEPS += \
./Library/not\ use/src/my_keypad.d 


# Each subdirectory must supply rules for building sources it contributes
Library/not\ use/src/my_keypad.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/Library/not\ use/src/my_keypad.c Library/not\ use/src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/uart3/../library/src" -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/uart3/../Library/inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"Library/not use/src/my_keypad.d" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Library-2f-not-20-use-2f-src

clean-Library-2f-not-20-use-2f-src:
	-$(RM) ./Library/not\ use/src/my_keypad.cyclo ./Library/not\ use/src/my_keypad.d ./Library/not\ use/src/my_keypad.o ./Library/not\ use/src/my_keypad.su

.PHONY: clean-Library-2f-not-20-use-2f-src

