#!/bin/sh
# Download the CMSIS core and STM32F4 device headers needed by the firmware
# (Apache-2.0, ARM / STMicroelectronics) into build/cmsis.
set -e
DST="${1:-$(dirname "$0")/../build/cmsis}"
CORE=https://raw.githubusercontent.com/ARM-software/CMSIS_5/develop/CMSIS/Core/Include
DEV=https://raw.githubusercontent.com/STMicroelectronics/cmsis_device_f4/master/Include
mkdir -p "$DST/Include" "$DST/Device/ST/STM32F4xx/Include"
for f in core_cm4.h cmsis_version.h cmsis_compiler.h cmsis_gcc.h mpu_armv7.h; do
  curl -fsSL "$CORE/$f" -o "$DST/Include/$f"
done
for f in stm32f4xx.h stm32f405xx.h system_stm32f4xx.h; do
  curl -fsSL "$DEV/$f" -o "$DST/Device/ST/STM32F4xx/Include/$f"
done
echo "CMSIS headers in $DST"
