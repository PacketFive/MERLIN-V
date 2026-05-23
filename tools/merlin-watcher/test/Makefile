# SPDX-License-Identifier: GPL-2.0-or-later
#
# Out-of-tree kernel module that registers a stub 'merlin_nl' Genl
# family and emits synthetic events on a timer.  Used to develop
# merlin-watcher without waiting for the in-tree kernel/merlin/
# module to ship.

obj-m := stub_genl.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
$(MAKE) -C $(KDIR) M=$(PWD) clean
