# SPDX-License-Identifier: GPL-2.0-only
#
# MERLIN-V out-of-tree build.
#
# In-tree (against bpf-next): include from kernel/Makefile.
# Out-of-tree: run `make -C /path/to/kernel M=$(pwd)`.

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

ifdef KERNELDIR
obj-m += merlin.o

merlin-y := \
	syscall.o    \
	core.o       \
	loader.o     \
	verifier.o   \
	decode.o     \
	dispatch.o   \
	maps.o

merlin-y                           += jit/select.o
merlin-$(CONFIG_MERLIN_JIT_X86_64) += jit/arch/x86_64.o
merlin-$(CONFIG_MERLIN_JIT_RISCV)  += jit/pass_through.o

ccflags-y := -I$(src)/include \
             -I$(src)/../../docs/design

merlin-$(CONFIG_MERLIN_SELFTEST) += selftest.o

.PHONY: all clean

all:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) modules \
		CONFIG_MERLIN=m \
		CONFIG_MERLIN_JIT_X86_64=$(if $(filter x86_64,$(shell uname -m)),y,n) \
		CONFIG_MERLIN_JIT_RISCV=$(if $(filter riscv%,$(shell uname -m)),y,n)

clean:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) clean

else
# In-tree: object list is driven by the Kconfig logic above via obj-$(CONFIG_MERLIN).
endif
