MODULE_NAME = fs_monitor
MODULE_FILE := $(MODULE_NAME).ko

obj-m += $(MODULE_NAME).o

fs_monitor-y := main.o service.o tracers.o # and something else

ccflags-y += -Wno-unused-variable

# if kernel version < 6.0, add base64.o
KERNEL_VER := $(shell uname -r)
KERNEL_MAJOR := $(shell echo $(KERNEL_VER) | cut -d '.' -f1)
KERNEL_MINOR := $(shell echo $(KERNEL_VER) | cut -d '.' -f2)

ifeq ($(shell [ $(KERNEL_MAJOR) -lt 6 ] || ([ $(KERNEL_MAJOR) -eq 6 ] && [ $(KERNEL_MINOR) -eq 0 ]) && echo 1), 1)
	fs_monitor-y += base64.o
endif

all:
	@make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install: $(MODULE_FILE)
	install -p -m 644 $(MODULE_FILE) /lib/modules/$(shell uname -r)/kernel/fs/
	@depmod -a

$(MODULE_FILE):
	@echo "Please run 'make' first"
	@false

uninstall:
	rm -f /lib/modules/$(shell uname -r)/kernel/fs/$(MODULE_FILE)
	@depmod -a

clean:
	@make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

.PHONY: all install uninstall clean
