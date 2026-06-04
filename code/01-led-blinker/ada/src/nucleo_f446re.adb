package body Nucleo_F446RE is

   procedure Delay_Ms (Milliseconds : in Delay_Ms_T) is
      CYCLES       : constant UInt32 :=
        (HSI_VALUE / 1000) * UInt32 (Milliseconds);
      Reload_Value : Register
      with Address => SYSTICK_LOAD, Volatile;
      Tick_Value   : Register
      with Address => SYSTICK_VAL, Volatile;
      Ctrl_Value   : Register
      with Address => SYSTICK_CTRL, Volatile;
   begin
      Reload_Value := Register (CYCLES);
      Tick_Value := Register (0);

      Ctrl_Value := Register (SYSTICK_CTRL_ENABLE or SYSTICK_CTRL_CLKSOURCE);

      loop
         exit when (Ctrl_Value and Register (SYSTICK_CTRL_COUNTFLAG)) /= 0;
      end loop;

      Ctrl_Value := Register (0);
   end Delay_Ms;

   procedure Led_Init is
      Rcc_Ahb1enr_Value : Register
      with Address => RCC_AHB1ENR, Volatile;
      Gpioa_Moder_Value : Register
      with Address => GPIOA_MODER, Volatile;
   begin
      Rcc_Ahb1enr_Value := Rcc_Ahb1enr_Value or Register (RCC_AHB1ENR_GPIOAEN);

      Gpioa_Moder_Value :=
        (Gpioa_Moder_Value
         and
           not Register (Shift_Left (GPIO_MODE_MASK, Natural (LED_PIN * 2))));
      Gpioa_Moder_Value :=
        (Gpioa_Moder_Value
         or Register (Shift_Left (GPIO_MODE_OUTPUT, Natural (LED_PIN * 2))));
   end Led_Init;

   procedure Led_Toggle is
      Gpioa_Odr_Value : Register
      with Address => GPIOA_ODR, Volatile;
   begin
      Gpioa_Odr_Value :=
        Gpioa_Odr_Value
        xor Register (Shift_Left (UInt32'(1), Natural (LED_PIN)));
   end Led_Toggle;

   procedure Button_Init is
      Rcc_Ahb1enr_Value : Register
      with Address => RCC_AHB1ENR, Volatile;
      Gpioc_Moder_Value : Register
      with Address => GPIOC_MODER, Volatile;
   begin
      -- Enable GPIOC clock
      Rcc_Ahb1enr_Value := Rcc_Ahb1enr_Value or Register (RCC_AHB1ENR_GPIOCEN);

      -- Set Button pin as input
      Gpioc_Moder_Value :=
        (Gpioc_Moder_Value
         and
           not Register
                 (Shift_Left (GPIO_MODE_MASK, Natural (BUTTON_PIN * 2))));
   end Button_Init;

   function Button_Is_Pressed return Boolean is
      Gpioc_Idr_Value : Register
      with Address => GPIOC_IDR, Volatile;
      State            : Register;
   begin
      -- Read Button pin state
      State :=
        (Gpioc_Idr_Value
         and Register (Shift_Left (UInt32'(1), (Natural (BUTTON_PIN)))));
      return State = 0; -- Active low button
   end Button_Is_Pressed;

end Nucleo_F446RE;
