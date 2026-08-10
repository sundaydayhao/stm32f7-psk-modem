# STM32F7 PSK Modem Debug Handoff

This file records the local environment, current debug state, and next steps so a new Codex session can continue without rediscovering everything.

## Local Environment

- Project: `C:\projects\stm32f7-psk-modem`
- Board: `STM32F769I-DISCO`
- Audio path: PC/headphone output -> board Line In -> WM8994 -> SAI1B RX DMA
- UART log: ST-LINK VCP on `COM9`, `115200 8N1`
- Flash/reset path: ST-LINK over `SWD`; do not flash "to COM9"
- STM32CubeProgrammer GUI:
  `C:\ProgramData\STM32CubeProgrammer\bin\STM32CubeProgrammer.exe`
- STM32CubeProgrammer CLI:
  `C:\ProgramData\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe`
- Reference input text:
  `C:\projects\psk-script\input.txt`
- Generated BPSK31 WAV:
  `C:\projects\psk-script\generated-wav\BPSK31.wav`
- Reference projects:
  `C:\projects\esp32s3-psk-modem`
  `C:\projects\fldigi`

## Build And Flash

Build:

```powershell
cd C:\projects\stm32f7-psk-modem
cmake --build --preset debug
```

Flash and reset:

```powershell
cd C:\projects\stm32f7-psk-modem
& 'C:\ProgramData\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe' -c port=SWD -w 'build\debug\stm32f769i_disco_psk_modem.elf' -v -rst
```

Open COM9 briefly:

```powershell
$port = [System.IO.Ports.SerialPort]::new('COM9', 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 200
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
try {
  Start-Sleep -Milliseconds 300
  $port.DiscardInBuffer()
  $start = Get-Date
  $line = ''
  while (((Get-Date) - $start).TotalSeconds -lt 20) {
    $s = $port.ReadExisting()
    if ($s.Length -gt 0) {
      foreach ($ch in $s.ToCharArray()) {
        if ($ch -eq "`n") {
          if ($line -match '\[rx31\]' -or $line -match '\[diag\]' -or $line -match '\[boot\]' -or $line -match '\[audio\]') { $line }
          $line = ''
        } elseif ($ch -ne "`r") { $line += $ch }
      }
    }
    Start-Sleep -Milliseconds 20
  }
} finally {
  if ($port.IsOpen) { $port.Close() }
}
```

## Current Firmware State

- Audio capture works.
- Spectrum/waterfall render works.
- Current decoded sample rate is `8192 Hz`.
- Audio codec rate is `49152 Hz`; firmware decimates by `6`.
- SAI was fixed to a 2-slot stereo frame setup.
- Current tone in `Core/Src/main.c`: `rx_tone_hz = 1526`.
- User source tone is nominally `1500 Hz`; captured PCM and spectrum show about `1520 Hz`, and offline decode scans preferred about `1526 Hz`.
- `PCM_DUMP_ENABLE` is currently `0`.
- PCM dump code remains in `Core/Src/main.c`.
- `tools\capture_pcm.ps1` can capture `[pcm16]` UART dumps when `PCM_DUMP_ENABLE` is set to `1`.
- Current main BPSK31 feed path uses `simple_bpsk_rx_feed()` in `third_party\psk_rtty\psk_rtty.c`.
- `pskcore_rx_feed()` is still kept in the file as a reference path but is not currently active.

## Important Captures

- `capture_8192_s16le.pcm`: short s16le mono PCM, `8192 Hz`
- `capture_8192_s16le_8s.pcm`: 8 second s16le mono PCM, `8192 Hz`
- Offline analysis of `capture_8192_s16le_8s.pcm`:
  - no clipping in the useful capture
  - FFT peak around `1501 Hz` in the raw capture
  - best Goertzel/fixed-symbol decode around `1525.5-1526.5 Hz`
  - fixed-symbol decode can recover the prefix `One warm and sunny ...`

## What Was Fixed

- SAI DMA is running and producing changing samples.
- Initial all-zero samples were fixed by correcting the SAI slot/frame setup.
- The 750 Hz / 1500 Hz confusion came from earlier audio framing/decimation interpretation. Current captured and displayed signal is around 1500 Hz.
- UI no longer only says "Waiting for audio DMA" when audio blocks are present.
- The waterfall/spectrum display is functional.
- A PCM export mechanism was added for offline analysis.
- The decoder now produces recognizable English fragments instead of only random-looking bytes.

## Current Symptoms

- Decode is improved but not correct enough to match `input.txt`.
- If the board resets while BPSK audio is already playing, the fixed-symbol decoder may start in the middle of a stream and choose the wrong symbol/varicode boundary.
- In that case the log can contain recognizable words mixed with bad bytes.
- Current behavior is better when audio starts cleanly after decoder reset, but still needs robust symbol acquisition.

## Findings

- This does not currently look like a floating point precision problem.
- STM32F769 has an FPU, and offline analysis of board-captured PCM proves the signal is decodable.
- CMSIS-DSP may still be useful later for FIR decimation, Goertzel, or FFT, but it is not the primary blocker.
- Main blockers are acquisition, symbol timing, bit polarity decisions, and decoder reset/start policy.
- The ESP32S3 reference project uses the same general `psk_rtty` core style; it does not magically solve the acquisition issue.

## Current Decoder Notes

- `generate_psk_wavs.py` fldigi-compatible BPSK mapping:
  - bit `0` causes 180 degree phase reversal
  - bit `1` keeps phase
- Offline fixed-symbol test against board PCM used:
  - sample rate `8192`
  - tone about `1526 Hz`
  - symbol rate `31.25`
  - integrate the middle part of each symbol, roughly 192 samples
  - invert the simple dot-product bit result to recover `One warm and sunny ...`
- Current firmware implementation is a pragmatic fixed-symbol decoder, not the final robust solution.

## Suggested Next Steps

1. Add a proper BPSK31 acquisition stage.
2. On a new signal, buffer a short window and test multiple symbol phases, for example 8 or 16 phase offsets across one symbol.
3. Score each phase by:
   - number of valid varicode delimiters
   - printable ASCII ratio
   - common English fragments from the expected story when testing with `input.txt`
4. Pick the best phase, reset the decoder at that phase, then emit text.
5. Add tone auto-lock around `1450-1550 Hz` using Goertzel or FFT peak tracking, but keep manual `1526 Hz` as a known-good default.
6. Once decode is stable, reduce diagnostic UART spam and keep PCM dump disabled by default.

## Useful Test Procedure

For repeatable tests, start from a reset and then play the WAV:

```powershell
$wav = 'C:\projects\psk-script\generated-wav\BPSK31.wav'
$cli = 'C:\ProgramData\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
$sp = [System.Media.SoundPlayer]::new($wav)
$port = [System.IO.Ports.SerialPort]::new('COM9', 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 100
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
try {
  $port.DiscardInBuffer()
  & $cli -c port=SWD -rst | Out-Null
  $start = Get-Date
  $played = $false
  $line = ''
  while (((Get-Date) - $start).TotalSeconds -lt 30) {
    if (-not $played -and ((Get-Date) - $start).TotalSeconds -gt 8) {
      $sp.Play()
      $played = $true
      '[host] play BPSK31.wav'
    }
    $s = $port.ReadExisting()
    if ($s.Length -gt 0) {
      foreach ($ch in $s.ToCharArray()) {
        if ($ch -eq "`n") {
          if ($line -match '\[rx31\]' -or $line -match '\[diag\]' -or $line -match '\[boot\]' -or $line -match '\[audio\]') { $line }
          $line = ''
        } elseif ($ch -ne "`r") { $line += $ch }
      }
    }
    Start-Sleep -Milliseconds 20
  }
  if ($line.Length -gt 0) { $line }
} finally {
  $sp.Stop()
  if ($port.IsOpen) { $port.Close() }
}
```

## Files Most Relevant To Continue

- `Core\Src\main.c`: top-level loop, audio gate, diagnostics, PCM dump, modem config
- `app\platform\audio_input.c`: SAI DMA, stereo frame handling, decimation
- `app\platform\audio_input.h`: audio rates and block sizes
- `third_party\psk_rtty\psk_rtty.c`: BPSK/RTTY decoder backend
- `app\modem\psk_modem.c`: wrapper around `psk_rtty`
- `app\ui\display.c`: spectrum/waterfall and decoded text display
- `tools\capture_pcm.ps1`: UART PCM capture helper

## Do Not Forget

- COM9 is UART logging only.
- Flash over SWD using STM32CubeProgrammer CLI.
- If COM9 cannot open, another terminal/serial monitor probably owns it.
- The user expects Chinese responses and wants hands-on debugging, build, flash, and test rather than only theory.
- Do not delete or revert the dirty working tree unless explicitly asked.
