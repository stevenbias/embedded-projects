# LED Blinker in Ada

This is the Ada implementation of the LED blinker project for the STM32F446RE microcontroller (NUCLEO-F446RE board).

## Comparison with C Version

| Metric | C | Ada |
|--------|---|-----|
| **BIN size** | 244 bytes | 252 bytes |
| **ELF size** | 5,732 bytes | 5,772 bytes |
| **Text section** | 244 bytes | 252 bytes |
| **Dependencies** | None | light-cortex-m4f runtime |

Ada is only 8 bytes larger than C (3.3% overhead) - minimal considering the type-safety benefits.

## Project Structure

```
ada/
├── README.md              # This file
├── led_blinker.gpr        # GPRbuild project file
├── led_blinker.adc        # Configuration pragmas (No_Run_Time)
├── Makefile
├── src/
│   ├── crt0.S             # Symlink to ../../common/crt0.s
│   ├── led_blinker.adb    # Main procedure (~25 lines)
│   ├── nucleo_f446re.ads  # Register definitions (~129 lines)
│   └── nucleo_f446re.adb  # Hardware abstraction (~43 lines)
├── obj/                   # Build artifacts
├── led-blinker.elf        # ELF binary
└── led-blinker.bin        # Binary for flashing/simulation
```

## Build Prerequisites

- **GNAT** - ARM cross-compiler (`arm-eabi` target)
- **light-cortex-m4f** - Minimal Ada runtime for Cortex-M4F
- **arm-eabi-objcopy** - For binary conversion

## Building

```bash
make          # Release build (default)
make debug    # Debug build with -g3 -Og
```

Or directly with GPRbuild:

```bash
gprbuild -P led_blinker.gpr
gprbuild -P led_blinker.gpr -XBUILD_MODE=DEBUG
```

## Output Files

- ELF: `led-blinker.elf`
- Binary: `led-blinker.bin` (created via `arm-eabi-objcopy`)

## Code Overview

### Main Procedure (`led_blinker.adb`)

```ada
with Nucleo_F446RE; use Nucleo_F446RE;

procedure Led_Blinker is
   Blink_Fast_Ms         : constant Delay_Ms_T := 100;
   Blink_Slow_Ms         : constant Delay_Ms_T := 500;
   Is_Fast               : Boolean := False;
   Current_Button_State  : Boolean := False;
   Previous_Button_State : Boolean := False;
begin
   Led_Init;
   Button_Init;

   loop
      Current_Button_State := Button_Is_Pressed;

      if Current_Button_State and not Previous_Button_State then
         Is_Fast := not Is_Fast;
         Delay_Ms (200);
      end if;

      Previous_Button_State := Current_Button_State;
      Led_Toggle;
      Delay_Ms (if Is_Fast then Blink_Fast_Ms else Blink_Slow_Ms);
   end loop;
end Led_Blinker;
```

### Register Type Definitions (`nucleo_f446re.ads`)

Ada's strong typing eliminates raw pointer arithmetic and bit manipulation:

```ada
---- GPIO Register Types ----
type MODER_Val_T is (INPUT, OUTPUT, AF, ANALOG) with Size => 2;
for MODER_Val_T use (INPUT => 0, OUTPUT => 1, AF => 2, ANALOG => 3);

type MODER_T is array (IO_Range) of MODER_Val_T
with Component_Size => MODER_Val_T'Size, Size => 32, Volatile_Full_Access;

type IDR_T is array (IO_Range) of Boolean
with Component_Size => 1, Size => 16, Volatile_Full_Access;

type ODR_T is array (IO_Range) of Boolean
with Component_Size => 1, Size => 16, Volatile_Full_Access;

type GPIO_T is record
   MODER : MODER_T;
   IDR   : IDR_T;
   ODR   : ODR_T;
end record;
for GPIO_T use
  record
    MODER at GPIOx_MODER_OFFSET range 0 .. 31;
    IDR   at GPIOx_IDR_OFFSET   range 0 .. 15;
    ODR   at GPIOx_ODR_OFFSET   range 0 .. 15;
  end record;

---- Peripheral Instances ----
GPIOA : GPIO_T with Address => GPIOA_BASE;
GPIOC : GPIO_T with Address => GPIOC_BASE;
```

### Hardware Abstraction (`nucleo_f446re.adb`)

Clean, readable hardware access without bit masks:

```ada
procedure Led_Init is
begin
   RCC.AHB1ENR.GPIOA_EN := True;
   GPIOA.MODER (LED_PIN) := OUTPUT;
end Led_Init;

procedure Led_Toggle is
begin
   GPIOA.ODR (LED_PIN) := not GPIOA.ODR (LED_PIN);
end Led_Toggle;

function Button_Is_Pressed return Boolean is
begin
   return (GPIOC.IDR (BUTTON_PIN) = False);
end Button_Is_Pressed;
```

## Key Differences from C

| Aspect | C | Ada |
|--------|---|-----|
| Build system | Make + GCC | GPRbuild + GNAT |
| Startup | Shared `common/crt0.s` | Shared (symlink) |
| Linker script | `common/linker.ld` | Shared |
| Type safety | Manual bit manipulation | Record representation clauses |
| Register access | Raw pointers + masks | Typed arrays with Address clauses |
| Binary size | 244 bytes | 252 bytes |

## Features

- **Strong typing for registers** - Record representation clauses map Ada types to hardware registers
- **No raw pointer arithmetic** - Register access via typed arrays and records
- **Type-safe GPIO pin indexing** - `IO_Range` subtype constrains pin numbers to 0..15
- **Shared startup code** - Symlink to `common/crt0.s`
- **Shared linker script** - Uses `common/linker.ld`
- **No_Run_Time pragma** - Minimal runtime with no elaboration overhead

### Preelaborate Aspect

The `Nucleo_F446RE` package uses the `Preelaborate` aspect:

```ada
package Nucleo_F446RE with Preelaborate is
```

`Preelaborate` is an Ada categorization pragma that:
- **Guarantees no elaboration code** - The package can be elaborated at compile-time, not runtime
- **Enables static initialization** - All package-level objects must be statically determinable
- **Reduces startup overhead** - Critical for bare-metal systems where `crt0` doesn't call elaboration routines
- **Restricts dependencies** - Can only depend on other `Preelaborate` or `Pure` packages

This is essential because our `crt0.S` jumps directly to `_ada_led_blinker` without calling `__gnat_runtime_initialize` or any elaboration procedures.

#### Preelaborate vs Pure

| Aspect | `Pure` | `Preelaborate` |
|--------|--------|----------------|
| **State** | No package-level state allowed | Package-level state allowed (if static) |
| **Side effects** | None permitted | None during elaboration |
| **Use case** | Math libraries, type definitions | Hardware register mappings, constants |
| **This project** | Too restrictive (we need `GPIOA`, `RCC` variables) | Ideal choice |

`Pure` would be too restrictive here since `Nucleo_F446RE` declares package-level variables (`GPIOA`, `GPIOC`, `RCC`, etc.) mapped to hardware addresses.

## File Details

### `led_blinker.gpr`

```
project Led_Blinker is
   for Target use "arm-eabi";
   for Runtime ("ada") use "light-cortex-m4f";

   package Compiler is
      for Default_Switches ("ada") use (
         "-mcpu=cortex-m4", "-mthumb",
         "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
         "-Os", "-flto",
         "-gnatp"   -- Suppress all runtime checks
      );
   end Compiler;

   package Linker is
      for Switches ("Ada") use (
         "-T", "../common/linker.ld",
         "-nostdlib", "-nostartfiles"
      );
   end Linker;
end Led_Blinker;
```

### `led_blinker.adc`

```ada
pragma No_Run_Time;
```

This pragma disables the Ada runtime, producing bare-metal code comparable to C.

### `common/linker.ld` (shared with C and Rust)

Custom linker script that:
- Places vector table at `0x08000000`
- Defines FLASH (1MB) and RAM (128KB) regions
- Defines `.text`, `.rodata`, `.data`, `.bss` sections
- Provides `_sdata`, `_edata`, `_lma_sdata`, `_sbss`, `_ebss` symbols
