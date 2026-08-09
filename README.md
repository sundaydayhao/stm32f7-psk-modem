# STM32F769I-DISCO PSK Modem

Bare-metal CMake project for STM32F769I-DISCO. Audio enters from Line IN through the WM8994 codec, is captured by SAI1B DMA at 48 kHz, decimated to 8 kHz, decoded as BPSK31/PSK63, and rendered with a spectrum view plus decoded text.

## Build

Install `arm-none-eabi-gcc`, CMake, and Ninja, then run:

```powershell
cmake --preset gcc-arm-debug
cmake --build --preset debug
```

The build is independent of Zephyr, ESP-IDF, and the ESP32 reference project. The reference code reused here is the platform-neutral PSK/RTTY decoder core from `esp32s3-psk-modem`.

## Notes

- CPU is configured for 216 MHz with hard-float Cortex-M7 flags.
- The audio DSP path is isolated in `app/platform/audio_input.c`; replace the current 6:1 averager with a CMSIS-DSP FIR decimator when a local CMSIS-DSP package is added.
- The display path writes RGB565 directly to SDRAM framebuffer. If the panel does not wake on a given board revision, add the OTM8009A DSI init sequence inside `display_init()`.
