#![no_std]
#![no_main]

const RCC_BASE: u32 = 0x40023800;
const GPIOA_BASE: u32 = 0x40020000;
const SYSTICK_BASE: u32 = 0xE000E010;

const LED_PIN: u32 = 5;
const HSI_CLOCK_HZ: u32 = 16_000_000;

const RCC_AHB1ENR: *mut u32 = (RCC_BASE + 0x30) as *mut u32;
const GPIOA_MODER: *mut u32 = (GPIOA_BASE + 0x00) as *mut u32;
const GPIOA_ODR: *mut u32 = (GPIOA_BASE + 0x14) as *mut u32;

const SYSTICK_CTRL: *mut u32 = (SYSTICK_BASE + 0x00) as *mut u32;
const SYSTICK_LOAD: *mut u32 = (SYSTICK_BASE + 0x04) as *mut u32;
const SYSTICK_VAL: *mut u32 = (SYSTICK_BASE + 0x08) as *mut u32;

#[unsafe(no_mangle)]
pub fn main() {
    unsafe {
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 0));
        GPIOA_MODER.write_volatile((GPIOA_MODER.read_volatile() & !(0x3 << (LED_PIN * 2))) | (0x1 << (LED_PIN * 2)));
    }

    loop {
        unsafe {
            GPIOA_ODR.write_volatile(GPIOA_ODR.read_volatile() ^ (1 << LED_PIN));
        }
        delay_ms(500);
    }
}

fn delay_ms(ms: u32) {
    let cycles = (HSI_CLOCK_HZ / 1000) * ms;
    unsafe {
        SYSTICK_LOAD.write_volatile(cycles & 0xFFFFFF);
        SYSTICK_VAL.write_volatile(0);
        SYSTICK_CTRL.write_volatile(0b101);
        while SYSTICK_CTRL.read_volatile() & 0x10000 == 0 {}
        SYSTICK_CTRL.write_volatile(0);
    }
}

#[panic_handler]
fn _panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
