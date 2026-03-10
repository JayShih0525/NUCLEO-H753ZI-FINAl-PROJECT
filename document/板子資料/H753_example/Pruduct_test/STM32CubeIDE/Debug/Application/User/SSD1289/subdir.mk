################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (12.3.rel1)
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
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../USB_DEVICE/App -I"D:/project/H753_example/Pruduct_test/Middlewares/Third_Party/LwIP/src/include/lwip/apps" -I"D:/project/H753_example/Pruduct_test/STM32CubeIDE/Application/User/Core" -I"D:/project/H753_example/Pruduct_test/STM32CubeIDE/Application/User/SSD1289" -I../../USB_DEVICE/Target -I../../Core/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc -I../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../../Drivers/CMSIS/Include -I../../FATFS/Target -I../../FATFS/App -I../../Middlewares/Third_Party/FatFs/src -I../../LWIP/App -I../../LWIP/Target -I../../Middlewares/Third_Party/LwIP/src/include -I../../Middlewares/Third_Party/LwIP/system -I../../Drivers/BSP/Components/lan8742 -I../../Middlewares/Third_Party/LwIP/src/include/netif/ppp -I../../Middlewares/Third_Party/LwIP/src/include/lwip -I../../Middlewares/Third_Party/LwIP/src/include/lwip/apps -I../../Middlewares/Third_Party/LwIP/src/include/lwip/priv -I../../Middlewares/Third_Party/LwIP/src/include/lwip/prot -I../../Middlewares/Third_Party/LwIP/src/include/netif -I../../Middlewares/Third_Party/LwIP/src/include/compat/posix -I../../Middlewares/Third_Party/LwIP/src/include/compat/posix/arpa -I../../Middlewares/Third_Party/LwIP/src/include/compat/posix/net -I../../Middlewares/Third_Party/LwIP/src/include/compat/posix/sys -I../../Middlewares/Third_Party/LwIP/src/include/compat/stdc -I../../Middlewares/Third_Party/LwIP/system/arch -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Application-2f-User-2f-SSD1289

clean-Application-2f-User-2f-SSD1289:
	-$(RM) ./Application/User/SSD1289/SSD1289.cyclo ./Application/User/SSD1289/SSD1289.d ./Application/User/SSD1289/SSD1289.o ./Application/User/SSD1289/SSD1289.su ./Application/User/SSD1289/SSD1289_STM32F753xx_proting.cyclo ./Application/User/SSD1289/SSD1289_STM32F753xx_proting.d ./Application/User/SSD1289/SSD1289_STM32F753xx_proting.o ./Application/User/SSD1289/SSD1289_STM32F753xx_proting.su ./Application/User/SSD1289/SSD1289_driver.cyclo ./Application/User/SSD1289/SSD1289_driver.d ./Application/User/SSD1289/SSD1289_driver.o ./Application/User/SSD1289/SSD1289_driver.su ./Application/User/SSD1289/SSD1289_string.cyclo ./Application/User/SSD1289/SSD1289_string.d ./Application/User/SSD1289/SSD1289_string.o ./Application/User/SSD1289/SSD1289_string.su

.PHONY: clean-Application-2f-User-2f-SSD1289

