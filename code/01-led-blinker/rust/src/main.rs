#![no_std]
#![no_main]

mod target;
use crate::target::*;

mod time;
use crate::time::*;

fn led_init() {
    unsafe {
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 0));
        GPIOA_MODER.write_volatile(
            (GPIOA_MODER.read_volatile() & !(0x3 << (LED_PIN * 2))) | (0x1 << (LED_PIN * 2)),
        );
    }
}

fn led_toggle() {
    unsafe { GPIOA_ODR.write_volatile(GPIOA_ODR.read_volatile() ^ (1 << LED_PIN)) };
}

#[unsafe(no_mangle)]
pub fn main() {
    led_init();
    loop {
        led_toggle();
        delay_ms(500);
    }
}

#[panic_handler]
fn _panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
