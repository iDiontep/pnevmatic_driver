################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Logic/Src/app.c 

OBJS += \
./Logic/Src/app.o 

C_DEPS += \
./Logic/Src/app.d 


# Each subdirectory must supply rules for building sources it contributes
Logic/Src/%.o Logic/Src/%.su Logic/Src/%.cyclo: ../Logic/Src/%.c Logic/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F373xC -c -I../Core/Inc -I../Drivers/STM32F3xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F3xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F3xx/Include -I../Drivers/CMSIS/Include -I../USB_DEVICE/App -I../USB_DEVICE/Target -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../Hardware_drivers/Inc -I../Logic/Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Logic-2f-Src

clean-Logic-2f-Src:
	-$(RM) ./Logic/Src/app.cyclo ./Logic/Src/app.d ./Logic/Src/app.o ./Logic/Src/app.su

.PHONY: clean-Logic-2f-Src

