source ~/.gdbinit

set debug arm

target ext :1234
set arch armv7e-m

mon reset halt

file led-blinker.elf
