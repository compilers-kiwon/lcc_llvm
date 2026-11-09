#include "c.h"
#undef yy
#define yy \
xx(alpha/osf,    alphaIR) \
xx(mips/irix,    mipsebIR) \
xx(sparc/sun,    sparcIR) \
xx(sparc/solaris,solarisIR) \
xx(x86/win32,    x86IR) \
xx(x86/linux,    x86linuxIR) \
xx(llvm/x86_64-linux,llvm_x86_64_linuxIR) \
xx(mlir/x86_64-linux,mlir_x86_64_linuxIR) \
xx(llvm/aarch64-linux,llvm_aarch64_linuxIR) \
xx(mlir/aarch64-linux,mlir_aarch64_linuxIR) \
xx(llvm/riscv64-linux,llvm_riscv64_linuxIR) \
xx(mlir/riscv64-linux,mlir_riscv64_linuxIR) \
xx(llvm/armhf-linux,llvm_armhf_linuxIR) \
xx(mlir/armhf-linux,mlir_armhf_linuxIR) \
xx(llvm/cortexm3-eabi,llvm_cortexm3_eabiIR) \
xx(mlir/cortexm3-eabi,mlir_cortexm3_eabiIR) \
xx(symbolic/osf, symbolic64IR) \
xx(symbolic/irix,symbolicIR) \
xx(symbolic,     symbolicIR) \
xx(bytecode,     bytecodeIR) \
xx(null,         nullIR)

#undef xx
#define xx(a,b) extern Interface b;
yy

Binding bindings[] = {
#undef xx
#define xx(a,b) #a, &b,
yy
	NULL, NULL
};
#undef yy
#undef xx
