obj-m += fs_monitor.o

fs_monitor-y := main.o service.o tracers.o # and something else

ccflags-y += -Wno-unused-variable

# if kernel version < 6.0, add base64.o
KERNEL_VER := $(shell uname -r)
KERNEL_MAJOR := $(shell echo $(KERNEL_VER) | cut -d '.' -f1)
KERNEL_MINOR := $(shell echo $(KERNEL_VER) | cut -d '.' -f2)

ifeq ($(shell [ $(KERNEL_MAJOR) -lt 6 ] || ([ $(KERNEL_MAJOR) -eq 6 ] && [ $(KERNEL_MINOR) -eq 0 ]) && echo 1), 1)
	fs_monitor-y += base64.o
endif

all: $(BUILD_DIR_MAKEFILE)
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
