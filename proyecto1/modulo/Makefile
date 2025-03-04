obj-m += sysinfo_202201444.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	# make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean