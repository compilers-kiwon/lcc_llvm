/*
 * The vector table of a Cortex-M3 program for qemu-system-arm -M
 * mps2-an385: the initial stack pointer, the top of the 16 MB PSRAM at
 * 0x21000000, and newlib's _start; any fault ends the program with
 * status 134.  It is compiled by arm-none-eabi-gcc, not by lcc, and
 * etc/cortexm3/cortexm3.ld puts it at address 0.
 */
extern void _start(void);
extern void _exit(int);
static void fault(void) { _exit(134); }
__attribute__((section(".vectors"), used))
void (*const vectors[16])(void) = {
	(void (*)(void))0x22000000, _start, fault, fault, fault, fault, fault, 0,
	0, 0, 0, fault, fault, 0, fault, fault
};
