# Renode Emulation for LED Blinker

This folder contains Renode configuration files to emulate the LED Blinker project on a Nucleo-F446RE board.

## Files

### nucleo-f446re.repl

Platform description file that defines the hardware configuration.

- Loads the STM32F4 base platform (CPU, GPIO, timers, UART, etc.)
- Defines `UserLED` connected to **PA5** (Arduino pin D13, green LED on Nucleo)
- Defines `UserButton` connected to **PC13** (user button on Nucleo)

The LED is connected to GPIO port A, pin 5 via the `gpioPortA:` section.

### led-blinker.resc

Renode script that sets up and runs the emulation.

- Creates a new machine named "Nucleo-F446RE"
- Loads the platform description from `nucleo-f446re.repl`
- Opens UART2 analyzer (ST-Link Virtual COM Port)
- Loads the compiled firmware ELF
- Starts execution automatically

## Requirements

- Renode installed
- Firmware compiled (C/Ada/Rust/Zig)

## Usage

```bash
renode -e '$bin=@path/to/binary.elf; $repl=@nucleo-f446re.repl; include @led-blinker.resc'
```

The emulation starts automatically. The UART analyzer window will open showing serial output.

### Useful Commands

Inside the Renode monitor:

| Command | Description |
|---------|-------------|
| `start` | Start/continue emulation |
| `pause` | Pause emulation |
| `UserLED State` | Check current LED state (True/False) |
| `logLevel -1 UserLED` | Log every LED state change |
| `sysbus.gpioPortA ReadDoubleWord 0x14` | Read GPIOA ODR register (bit 5 = LED) |
| `sysbus.uart2 ReadChar` | Read a character from UART2 |

### Check LED is working

After starting Renode:

```
(Nucleo-F446RE) logLevel -1 UserLED
```

You should see messages like:
```
[NOISY] UserLED: LED state changed to True
[NOISY] UserLED: LED state changed to False
```