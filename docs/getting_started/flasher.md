# WebSerial Flasher

Flash compiled Parakram bytecode directly to your ESP32-S3 from your browser. No drivers, no IDE, no Python tools needed.

## Requirements

- **Google Chrome** or **Microsoft Edge** (Firefox does not support WebSerial)
- An ESP32-S3 development board connected via USB
- Compiled bytecode from the Playground

## How It Works

1. **Generate firmware** in the Playground — type your hardware logic in plain English, review the IR, and compile.
2. **Navigate to the WebFlasher** — click "WebFlasher" in the navigation bar or go to `/flasher.html`.
3. **Connect your device** — click "Connect Target Device". Chrome will prompt you to select the USB serial port.
4. **Flash** — click "Flash Compiled Bytecode". The flasher streams the binary payload in 256-byte chunks over the serial connection at 115200 baud.
5. **Done** — the device reboots into your new firmware automatically.

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "WebSerial API not supported" | Use Chrome or Edge. Firefox/Safari don't support it. |
| No device appears in popup | Check USB cable, install CP2102/CH340 drivers if needed. |
| "No compiled bytecode found" | Go back to the Playground and generate + compile first. |
| Write fails mid-stream | Reduce baud rate or check USB cable quality. |
