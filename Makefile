obj-m := scull-z.o
scull-z-objs := scull.o

INDEX_FILES :=

.PHONY: all clean tags cscope cxx_files hpp_files
all:
	$(MAKE) -C $(SRC_KERNEL_DIR) M=$(PWD) modules

tags: cxx_files hpp_files

cscope: cxx_files hpp_files

clean:
	rm -rf .*.cmd *.o modules.order *.ko Module.symvers *.mod.*
