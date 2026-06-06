package body Nucleo_F446RE is

   procedure Delay_Ms (Milliseconds : in Delay_Ms_T) is
      CYCLES : constant Register_T :=
        (HSI_VALUE / 1000) * Register_T (Milliseconds);
   begin
      Systick_Load := CYCLES;
      Systick_Val := Register_T (0);

      Systick_Ctrl := (
         Enable     => True,
         Clk_Source => True,
         others => False
      );

      loop
         exit when (Systick_Ctrl.Count_Flag);
      end loop;
      Systick_Ctrl := (others => False);
   end Delay_Ms;

   procedure Led_Init is
   begin
      RCC.AHB1ENR.GPIOA_EN := True;
      GPIOA.MODER (LED_PIN) := OUTPUT;
   end Led_Init;

   procedure Led_Toggle is
   begin
      GPIOA.ODR (LED_PIN) := not GPIOA.ODR (LED_PIN);
   end Led_Toggle;

   procedure Button_Init is
   begin
      RCC.AHB1ENR.GPIOC_EN := True;
      GPIOC.MODER (BUTTON_PIN) := INPUT;
   end Button_Init;

   function Button_Is_Pressed return Boolean is
   begin
      return (GPIOC.IDR (BUTTON_PIN) = False);
   end Button_Is_Pressed;

end Nucleo_F446RE;
