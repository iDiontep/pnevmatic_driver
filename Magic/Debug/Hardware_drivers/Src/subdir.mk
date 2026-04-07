################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Hardware_drivers/Src/Hardware_drivers.c \
../Hardware_drivers/Src/receiver.c \
../Hardware_drivers/Src/tb6560.c 

OBJS += \
./Hardware_drivers/Src/Hardware_drivers.o \
./Hardware_drivers/Src/receiver.o \
./Hardware_drivers/Src/tb6560.o 

C_DEPS += \
./Hardware_drivers/Src/Hardware_drivers.d \
./Hardware_drivers/Src/receiver.d \
./Hardware_drivers/Src/tb6560.d 


# Each subdirectory must supply rules for building sources it contributes
Hardware_drivers/Src/%.o Hardware_drivers/Src/%.su Hardware_drivers/Src/%.cyclo: ../Hardware_drivers/Src/%.c Hardware_drivers/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F373xC -c -I../Core/Inc -I../Drivers/STM32F3xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F3xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F3xx/Include -I../Drivers/CMSIS/Include -I../USB_DEVICE/App -I../USB_DEVICE/Target -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../Hardware_drivers/Inc -I../Logic/Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Hardware_drivers-2f-Src

clean-Hardware_drivers-2f-Src:
	-$(RM) ./Hardware_drivers/Src/Hardware_drivers.cyclo ./Hardware_drivers/Src/Hardware_drivers.d ./Hardware_drivers/Src/Hardware_drivers.o ./Hardware_drivers/Src/Hardware_drivers.su ./Hardware_drivers/Src/receiver.cyclo ./Hardware_drivers/Src/receiver.d ./Hardware_drivers/Src/receiver.o ./Hardware_drivers/Src/receiver.su ./Hardware_drivers/Src/tb6560.cyclo ./Hardware_drivers/Src/tb6560.d ./Hardware_drivers/Src/tb6560.o ./Hardware_drivers/Src/tb6560.su

.PHONY: clean-Hardware_drivers-2f-Src

