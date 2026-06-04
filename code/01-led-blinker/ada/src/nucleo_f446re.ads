with System;

package Nucleo_F446RE is

   type UInt32 is mod 2 ** 32;
   for UInt32'Size use 32;
   pragma Provide_Shift_Operators (UInt32);

   type Register is mod 2**32 with Size => 32, Alignment => 1;

   RCC_BASE    : constant := 16#40023800#;
   RCC_AHB1ENR : constant System.Address := System'To_Address (RCC_BASE + 16#30#);

   RCC_AHB1ENR_GPIOAEN : constant UInt32 := 1;
   RCC_AHB1ENR_GPIOCEN : constant UInt32 := 16#04#;

   GPIO_MODE_INPUT  : constant UInt32 := 0;
   GPIO_MODE_OUTPUT : constant UInt32 := 1;
   GPIO_MODE_AF     : constant UInt32 := 2;
   GPIO_MODE_ANALOG : constant UInt32 := 3;
   GPIO_MODE_MASK   : constant UInt32 := 3;

   GPIOx_MODER_OFFSET : constant := 16#00#;
   GPIOx_IDR_OFFSET   : constant := 16#10#;
   GPIOx_ODR_OFFSET   : constant := 16#14#;

   GPIOA_BASE  : constant := 16#40020000#;
   GPIOA_MODER : constant System.Address :=
      System'To_Address (GPIOA_BASE + GPIOx_MODER_OFFSET);
   GPIOA_ODR   : constant System.Address :=
      System'To_Address (GPIOA_BASE + GPIOx_ODR_OFFSET);

   GPIOC_BASE  : constant := 16#40020800#;
   GPIOC_MODER : constant System.Address :=
      System'To_Address (GPIOC_BASE + GPIOx_MODER_OFFSET);
   GPIOC_IDR   : constant System.Address :=
      System'To_Address (GPIOC_BASE + GPIOx_IDR_OFFSET);

   SYSTICK_BASE : constant := 16#E000E010#;
   SYSTICK_CTRL : constant System.Address := System'To_Address (SYSTICK_BASE + 16#00#);
   SYSTICK_LOAD : constant System.Address := System'To_Address (SYSTICK_BASE + 16#04#);
   SYSTICK_VAL  : constant System.Address := System'To_Address (SYSTICK_BASE + 16#08#);

   SYSTICK_CTRL_ENABLE    : constant UInt32 := 16#0000_0001#;
   SYSTICK_CTRL_TICKINT   : constant UInt32 := 16#0000_0002#;
   SYSTICK_CTRL_CLKSOURCE : constant UInt32 := 16#0000_0004#;
   SYSTICK_CTRL_COUNTFLAG : constant UInt32 := 16#0001_0000#;
   SYSTICK_LOAD_MAX       : constant UInt32 := 16#00FF_FFFF#;

   LED_PIN    : constant UInt32 := 5;
   BUTTON_PIN : constant UInt32 := 13;

   HSI_VALUE : constant UInt32 := 16_000_000; -- 16 MHz

   type Delay_Ms_T is range 0 .. 1000;
   
   procedure Delay_Ms (Milliseconds : in Delay_Ms_T);
   procedure Led_Init;
   procedure Led_Toggle;

   procedure Button_Init;
   function Button_Is_Pressed return Boolean;

end Nucleo_F446RE;
