################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Application/User/SSD1289/SSD1289.c \
../Application/User/SSD1289/SSD1289_STM32F753xx_proting.c \
../Application/User/SSD1289/SSD1289_driver.c \
../Application/User/SSD1289/SSD1289_string.c 

OBJS += \
./Application/User/SSD1289/SSD1289.o \
./Application/User/SSD1289/SSD1289_STM32F753xx_proting.o \
./Application/User/SSD1289/SSD1289_driver.o \
./Application/User/SSD1289/SSD1289_string.o 

C_DEPS += \
./Application/User/SSD1289/SSD1289.d \
./Application/User/SSD1289/SSD1289_STM32F753xx_proting.d \
./Application/User/SSD1289/SSD1289_driver.d \
./Application/User/SSD1289/SSD1289_string.d 


# Each subdirectory must supply rules for building sources it contributes
Application/User/SSD1289/%.o Application/User/SSD1289/%.su Application/User/SSD1289/%.cyclo: ../Application/User/SSD1289/%.c Application/User/SSD1289/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../Core/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I"/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/LCD_8bits/STM32CubeIDE/Application/User/SSD1289" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Application-2f-User-2f-SSD1289

clean-Application-2f-User-2f-SSD1289:
	-$(RM) ./Application/User/SSD1289/SSD1289.cyclo ./Application/User/SSD1289/SSD1289.d ./Application/User/SSD1289/SSD1289.o ./Application/User/SSD1289/SSD1289.su ./Application/User/SSD1289/SSD1289_STM32F753xx_proting.cyclo ./Application/User/SSD1289/SSD1289_STM32F753xx_proting.d ./Application/User/SSD1289/SSD1289_STM32F753xx_proting.o ./Application/User/SSD1289/SSD1289_STM32F753xx_proting.su ./Application/User/SSD1289/SSD1289_driver.cyclo ./Application/User/SSD1289/SSD1289_driver.d ./Application/User/SSD1289/SSD1289_driver.o ./Application/User/SSD1289/SSD1289_driver.su ./Application/User/SSD1289/SSD1289_string.cyclo ./Application/User/SSD1289/SSD1289_string.d ./Application/User/SSD1289/SSD1289_string.o ./Application/User/SSD1289/SSD1289_string.su

.PHONY: clean-Application-2f-User-2f-SSD1289

