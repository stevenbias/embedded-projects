pub const RCC_BASE: u32 = 0x40023800;
pub const GPIOA_BASE: u32 = 0x40020000;
pub const SYSTICK_BASE: u32 = 0xE000E010;

pub const RCC_AHB1ENR: *mut u32 = (RCC_BASE + 0x30) as *mut u32;
pub const GPIOA_MODER: *mut u32 = (GPIOA_BASE + 0x00) as *mut u32;
pub const GPIOA_ODR: *mut u32 = (GPIOA_BASE + 0x14) as *mut u32;

pub const SYSTICK_CTRL: *mut u32 = (SYSTICK_BASE + 0x00) as *mut u32;
pub const SYSTICK_LOAD: *mut u32 = (SYSTICK_BASE + 0x04) as *mut u32;
pub const SYSTICK_VAL: *mut u32 = (SYSTICK_BASE + 0x08) as *mut u32;

pub const LED_PIN: u32 = 5;
pub const HSI_CLOCK_HZ: u32 = 16_000_000;
