TARGET = libdclink.a
OBJS = src/dc_link.o

EXTRA_CFLAGS = -I./include
CPPFLAGS += -I./include

include $(KOS_BASE)/Makefile.rules

all: $(TARGET)

$(TARGET): $(OBJS)
	$(KOS_AR) rcs $(TARGET) $(OBJS)
	$(KOS_RANLIB) $(TARGET)

clean:
	-rm -f $(TARGET) $(OBJS)
