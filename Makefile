obj-m		+= clearram.o
ccflags-y	+= -std=gnu99 -Wall
KSRC		:= /lib/modules/$(shell uname -r)/build
clearram.ko:	clearram.c
		make -C $(KSRC) M=$(PWD) modules
clean::
		make -C $(KSRC) M=$(PWD) clean
