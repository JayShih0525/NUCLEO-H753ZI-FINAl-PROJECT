################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (12.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/SSD1289/SSD1289.c \
../Drivers/SSD1289/SSD1289_STM32F753xx_proting.c \
../Drivers/SSD1289/SSD1289_driver.c 

C_DEPS += \
./Drivers/SSD1289/SSD1289.d \
./Drivers/SSD1289/SSD1289_STM32F753xx_proting.d \
./Drivers/SSD1289/SSD1289_driver.d 

OBJS += \
./Drivers/SSD1289/SSD1289.o \
./Drivers/SSD1289/SSD1289_STM32F753xx_proting.o \
./Drivers/SSD1289/SSD1289_driver.o 


# Each subdirectory must supply rules for building sources it contributes
Drivers/SSD1289/%.o Drivers/SSD1289/%.su Drivers/SSD1289/%.cyclo: ../Drivers/SSD1289/%.c Drivers/SSD1289/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../Core/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../TouchGFX/App -I../../TouchGFX/target/generated -I../../TouchGFX/target -I../../Middlewares/ST/touchgfx/framework/include -I../../TouchGFX/generated/fonts/include -I../../TouchGFX/generated/gui_generated/include -I../../TouchGFX/generated/images/include -I../../TouchGFX/generated/texts/include -I../../TouchGFX/generated/videos/include -I../../TouchGFX/gui/include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-SSD1289

clean-Drivers-2f-SSD1289:
	-$(RM) ./Drivers/SSD1289/SSD1289.cyclo ./Drivers/SSD1289/SSD1289.d ./Drivers/SSD1289/SSD1289.o ./Drivers/SSD1289/SSD1289.su ./Drivers/SSD1289/SSD1289_STM32F753xx_proting.cyclo ./Drivers/SSD1289/SSD1289_STM32F753xx_proting.d ./Drivers/SSD1289/SSD1289_STM32F753xx_proting.o ./Drivers/SSD1289/SSD1289_STM32F753xx_proting.su ./Drivers/SSD1289/SSD1289_driver.cyclo ./Drivers/SSD1289/SSD1289_driver.d ./Drivers/SSD1289/SSD1289_driver.o ./Drivers/SSD1289/SSD1289_driver.su

.PHONY: clean-Drivers-2f-SSD1289

