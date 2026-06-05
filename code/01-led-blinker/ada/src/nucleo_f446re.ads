with System;

package Nucleo_F446RE is
   ---- Types for 32-bit wide registers ----
   type Bits32_Range is range 0 .. 31;
   for Bits32_Range'Size use 32;

   subtype IO_Range is Bits32_Range range 0 .. 15;

   type Register_T is mod 2 ** 32 with Size => 32, Volatile;

   ---- Constants specific to the STM32F446RE ----
   HSI_VALUE  : constant := 16_000_000; -- 16 MHz
   LED_PIN    : constant IO_Range := 5;
   BUTTON_PIN : constant IO_Range := 13;

   ---- Register Offsets ----
   GPIOx_MODER_OFFSET : constant := 16#00#;
   GPIOx_IDR_OFFSET   : constant := 16#10#;
   GPIOx_ODR_OFFSET   : constant := 16#14#;

   RCC_AHB1ENR_OFFSET : constant := 16#30#;

   SYSTICK_CTRL_OFFSET : constant := 16#00#;
   SYSTICK_LOAD_OFFSET : constant := 16#04#;
   SYSTICK_VAL_OFFSET  : constant := 16#08#;

   ---- Peripheral Base Addresses ----
   SYSTICK_BASE : constant := 16#E000E010#;
   RCC_BASE     : constant System.Address := System'To_Address (16#40023800#);
   GPIOA_BASE   : constant System.Address := System'To_Address (16#40020000#);
   GPIOC_BASE   : constant System.Address := System'To_Address (16#40020800#);

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
   --  Define only the necessary fields for this example

   ---- RCC Register Types ----
   type AHB1ENR_T is record
      GPIOA_EN : Boolean;
      GPIOC_EN : Boolean;
   end record
   with Size => 32, Volatile_Full_Access;
   for AHB1ENR_T use
     record
       GPIOA_EN at 0 range 0 .. 0;
       GPIOC_EN at 0 range 2 .. 2;
     end record;

   type RCC_T is record
      AHB1ENR : AHB1ENR_T;
   end record;
   for RCC_T use
     record
       AHB1ENR at RCC_AHB1ENR_OFFSET range 0 .. 31;
     end record;
   --  Define only the necessary fields for this example

   ---- Systick Register Types ----
   type Systick_Ctrl_T is record
      Enable     : Boolean;
      Tick_Int   : Boolean;
      Clk_Source : Boolean;
      Count_Flag : Boolean;
   end record
   with Size => 32, Volatile_Full_Access;
   for Systick_Ctrl_T use
     record
       Enable     at 0 range 0 .. 0;
       Tick_Int   at 0 range 1 .. 1;
       Clk_Source at 0 range 2 .. 2;
       Count_Flag at 0 range 16 .. 16;
     end record;

   ---- Peripheral Instances ----
   GPIOA        : GPIO_T
   with Address => GPIOA_BASE, Volatile;
   GPIOC        : GPIO_T
   with Address => GPIOC_BASE, Volatile;
   RCC          : RCC_T
   with Address => RCC_BASE, Volatile;
   Systick_Ctrl : Systick_Ctrl_T
   with
     Address => System'To_Address (SYSTICK_BASE + SYSTICK_CTRL_OFFSET),
     Volatile_Full_Access;
   Systick_Load : Register_T
   with
     Address => System'To_Address (SYSTICK_BASE + SYSTICK_LOAD_OFFSET),
     Volatile_Full_Access;
   Systick_Val  : Register_T
   with
     Address => System'To_Address (SYSTICK_BASE + SYSTICK_VAL_OFFSET),
     Volatile_Full_Access;

   type Delay_Ms_T is range 0 .. 1000;

   procedure Led_Init;
   procedure Led_Toggle;

   procedure Button_Init;
   function Button_Is_Pressed return Boolean;

   procedure Delay_Ms (Milliseconds : in Delay_Ms_T);
end Nucleo_F446RE;
