KSRC			:= /lib/modules/$(shell uname -r)/build
TARGET			= clearram
ccflags-y		+= -std=gnu99 -Wall
clearram-Linux-objs	:= subr_amd64.o subr_Linux.o clearram.o
obj-m			+= clearram-Linux.o
clearram.ko:		subr_Linux.c clearram.c
			make -C $(KSRC) M=$(PWD) modules
clean::
			make -C $(KSRC) M=$(PWD) clean
