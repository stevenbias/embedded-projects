# Renode Emulation for LED Blinker

This folder contains Renode configuration files to emulate the LED Blinker project on a Nucleo-F446RE board.

## Files

### nucleo-f446re.repl

Platform description file that defines the hardware configuration.

- Loads the STM32F4 base platform (CPU, GPIO, timers, UART, etc.)
- Defines `UserLED` connected to **PA5** (Arduino pin D13, green LED on Nucleo)
- Defines `UserButton` connected to **PC13** (user button, active LOW)

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
| `machine Reset` | Reset the emulated machine (CPU + peripherals) |
| `UserLED State` | Check current LED state (True/False) |
| `logLevel -1 UserLED` | Log every LED state change |
| `UserButton Press` | Simulate pressing the user button |
| `UserButton Release` | Simulate releasing the user button |
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

### Testing the Button

The button on PC13 is active LOW (pressed = 0, released = 1).

> **Note on naming:** `UserButton` is registered on `sysbus` (not as a child of `gpioPortC`) so it can be accessed directly by its short name `UserButton`. If placed on `gpioPortC`, the command would be `gpioPortC.UserButton Press` instead.

To test the button-controlled blink speed toggle:

1. Start the emulation and enable LED logging:

```
(Nucleo-F446RE) logLevel -1 UserLED
```

2. Observe the LED blinking slowly (500ms period).

3. Simulate a button press to switch to fast blink (100ms):

```
(Nucleo-F446RE) UserButton Press
(Nucleo-F446RE) # wait ~200ms for firmware debounce delay
(Nucleo-F446RE) UserButton Release
```

You should see the LED toggling at a faster rate.

4. Press the button again to switch back to slow blink.

### GDB Debugging with Renode

Renode also provides a GDB server for debugging:

```bash
# Terminal 1: Start Renode with GDB stub enabled
renode -e '$$bin=@led-blinker.elf; $$repl=@../renode/nucleo-f446re.repl; include @../renode/led-blinker.resc; listen GDB 3333'
```

```bash
# Terminal 2: Connect with GDB
arm-none-eabi-gdb led-blinker.elf
```

```gdb
(gdb) target remote :3333
(gdb) break main
(gdb) continue

# Watch GPIOA_ODR toggle
(gdb) watch *0x40020014
(gdb) continue
```
