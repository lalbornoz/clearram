obj-m		+= clearram.o
ccflags-y	+= -std=gnu99 -Wall
KSRC		:= $(HOME)/.local/src/linux-source-4.7
clearram.ko:	clearram.c
		make -C $(KSRC) M=$(PWD) modules
clean::
		make -C $(KSRC) M=$(PWD) clean
