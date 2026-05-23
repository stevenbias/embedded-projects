use crate::target::*;

pub fn delay_ms(ms: u32) {
    let cycles = (HSI_CLOCK_HZ / 1000) * ms;
    unsafe {
        SYSTICK_LOAD.write_volatile(cycles & 0xFFFFFF);
        SYSTICK_VAL.write_volatile(0);
        SYSTICK_CTRL.write_volatile(0b101);
        while (SYSTICK_CTRL.read_volatile() & 0x10000) == 0 {}
        SYSTICK_CTRL.write_volatile(0);
    }
}
