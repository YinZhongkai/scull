obj-m := scull-base.o
scull-base-objs := scull_base.o

.PHONY: all clean
all:
	$(MAKE) -C $(SRC_KERNEL_DIR) M=$(PWD) modules

clean:
	rm -rf .*.cmd *.o modules.order *.ko Module.symvers *.mod.*
