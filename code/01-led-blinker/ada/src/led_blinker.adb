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
