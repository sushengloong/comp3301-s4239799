OBJ=fops.o

obj-m += $(OBJ)
MOD_DIR=/lib/modules/$(shell uname -r)/build

.PHONY: all
all:
	make -C $(MOD_DIR) M=$(PWD) modules

.PHONY: clean
clean:
	make -C $(MOD_DIR) M=$(PWD) clean
