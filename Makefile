CC = i686-elf-gcc -ffreestanding
AS = i686-elf-gcc -c -ffreestanding -Wa,--divide

CFLAGS += -DFREESTANDING_ENVIRONMENT
CFLAGS += -m32 -I. -I./sforth/ -g
CFLAGS += -DENGINE_32BIT -DCORE_CELLS_COUNT="32 * 1024" -DSTACK_DEPTH=32
# CFLAGS += -fomit-frame-pointer -fdata-sections -ffunction-sections 
KINIT_OBJECTS = kinit.o
KOBJECTS = klow.o kmain.o idt.o simple-console.o setjmp.o dictionary-ext.o \
	   init-pgdir-tab.o \
	   common-data.o \
	   fork.o

SFORTH_OBJECTS = sforth/engine.o sf-arch.o sforth/sf-opt-file.o sforth/sf-opt-string.o sforth/sf-opt-prog-tools.o
SFORTH_ESCAPED_CODE_FILES = arena.efs pci.efs init.efs ata.efs
# the start at the floppy image of the low-level kernel initialization code
#KINIT_START = 16384
# samsung nc110 flash drive
#KINIT_START = 32256
# floppy on virtualbox
KINIT_START = 18432

# the start at the floppy image of the kernel
#KERNEL_START = 32768
# samsung nc110 flash drive
#KERNEL_START = 64512
# floppy on virtualbox
KERNEL_START = 36864

# the total size of the floppy image
IMAGE_SIZE = 1474560
EXECUTABLE_EXTENSION=

all: utils $(SFORTH_ESCAPED_CODE_FILES) dt.img

debug:
	virtualbox --startvm dt --debug-command-line
run:
	virtualbox --startvm dt
dis:
	i686-elf-objdump -d -S kernel > dis.txt
dumps:
	objdump -x kernel.exe > img.txt
	objdump -d -S kernel.exe > dis.txt

%: %.s
	$(CC) -m32 -nostdlib -Wa,--divide -Wl,-eentry_point -T $(basename $@).ld -o $@ $<

%.bin: %
	objcopy -O binary $<$(EXECUTABLE_EXTENSION) $@

%.efs: %.fs
	./stresc < $^ > $@
utils: stresc

stresc: utils/stresc.c
	gcc -o $@ $<

kmain.o: kmain.c $(SFORTH_ESCAPED_CODE_FILES)

boot.bin: boot
	objcopy -O binary $<$(EXECUTABLE_EXTENSION) $@
pxeboot.0: pxeboot
	objcopy -O binary $<$(EXECUTABLE_EXTENSION) $@
#boot: boot.s
#$(CC) -m32 -nostdlib -Wl,-eentry_point -T boot.ld -o $@ $<
	

kernel.bin: kernel
	objcopy -O binary -R .init-pgdir-tab $<$(EXECUTABLE_EXTENSION) $@
kernel: $(KOBJECTS) $(SFORTH_OBJECTS)
	$(CC) -v -m32 -o $@ -T kernel.ld -nostdlib $^ -fno-exceptions -static -lgcc

kinit.bin: kinit
	objcopy -O binary $<$(EXECUTABLE_EXTENSION) $@
kinit: kinit.s
	$(CC) -m32 -nostdlib -Wa,--divide -Wl,-eentry_point -T boot.ld -o $@ $<

clean:
	-rm boot.bin boot pxeboot.0 pxeboot kinit.bin kinit kernel.bin kernel \
		$(KINIT_OBJECTS) $(KOBJECTS) $(SFORTH_OBJECTS) \
		$(SFORTH_ESCAPED_CODE_FILES)
	-rm 1.bin 2.bin 3.bin 4.bin
	-rm boot$(EXECUTABLE_EXTENSION) kinit$(EXECUTABLE_EXTENSION) kernel$(EXECUTABLE_EXTENSION)
	-rm dt.img

dt.img:	boot.bin pxeboot.0 kinit.bin kernel.bin
	objcopy -I binary -O binary --gap-fill 0 --pad-to $(KINIT_START) boot.bin 1.bin
	cat 1.bin kinit.bin > 2.bin
	objcopy -I binary -O binary --gap-fill 0 --pad-to $(KERNEL_START) 2.bin 3.bin
	cat 3.bin kernel.bin > 4.bin
	objcopy -I binary -O binary --gap-fill 0 --pad-to $(IMAGE_SIZE) 4.bin $@

