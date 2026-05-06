TARGET = test_dclink.elf
OBJS = src/dc_link.o test.o

KOS_ROMDISK_DIR = romdisk
include $(KOS_BASE)/Makefile.rules

all: $(TARGET)

include $(KOS_BASE)/Makefile.prefab

clean:
	-rm -f $(TARGET) $(OBJS) romdisk.o romdisk.img

test_dclink.elf: $(OBJS) romdisk.o
	$(KOS_CC) $(KOS_CFLAGS) $(KOS_LDFLAGS) -o $(TARGET) $(KOS_START) \
		$(OBJS) romdisk.o $(OBJEXTRA) -L$(KOS_BASE)/lib -l$(KOS_LIBS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
