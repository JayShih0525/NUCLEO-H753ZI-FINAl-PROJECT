################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/aes.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/cipher.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/cipher_wrap.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/gcm.c \
/Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/platform.c 

OBJS += \
./Core/mbedtls/aes.o \
./Core/mbedtls/cipher.o \
./Core/mbedtls/cipher_wrap.o \
./Core/mbedtls/gcm.o \
./Core/mbedtls/platform.o 

C_DEPS += \
./Core/mbedtls/aes.d \
./Core/mbedtls/cipher.d \
./Core/mbedtls/cipher_wrap.d \
./Core/mbedtls/gcm.d \
./Core/mbedtls/platform.d 


# Each subdirectory must supply rules for building sources it contributes
Core/mbedtls/aes.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/aes.c Core/mbedtls/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../library -I../../library/mbedtls/include -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Core/mbedtls/cipher.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/cipher.c Core/mbedtls/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../library -I../../library/mbedtls/include -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Core/mbedtls/cipher_wrap.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/cipher_wrap.c Core/mbedtls/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../library -I../../library/mbedtls/include -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Core/mbedtls/gcm.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/gcm.c Core/mbedtls/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../library -I../../library/mbedtls/include -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Core/mbedtls/platform.o: /Users/shihyenchieh/Downloads/NUCLEO-H753ZI-FINAl-PROJECT/library/mbedtls/library/platform.c Core/mbedtls/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H753xx -c -I../../library -I../../library/mbedtls/include -I../Core/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-mbedtls

clean-Core-2f-mbedtls:
	-$(RM) ./Core/mbedtls/aes.cyclo ./Core/mbedtls/aes.d ./Core/mbedtls/aes.o ./Core/mbedtls/aes.su ./Core/mbedtls/cipher.cyclo ./Core/mbedtls/cipher.d ./Core/mbedtls/cipher.o ./Core/mbedtls/cipher.su ./Core/mbedtls/cipher_wrap.cyclo ./Core/mbedtls/cipher_wrap.d ./Core/mbedtls/cipher_wrap.o ./Core/mbedtls/cipher_wrap.su ./Core/mbedtls/gcm.cyclo ./Core/mbedtls/gcm.d ./Core/mbedtls/gcm.o ./Core/mbedtls/gcm.su ./Core/mbedtls/platform.cyclo ./Core/mbedtls/platform.d ./Core/mbedtls/platform.o ./Core/mbedtls/platform.su

.PHONY: clean-Core-2f-mbedtls

