TARGET = udp_servo_control

CC		:= gcc
LINKER		:= gcc

WFLAGS		:= -Wall -Wextra -Werror=float-equal -Wuninitialized -Wunused-variable -Wdouble-promotion
CFLAGS		:= -g -c -Wall
LDFLAGS		:= -pthread -lm -lrt -l:librobotcontrol.so.1

SOURCES		:= $(wildcard *.c)
INCLUDES	:= $(wildcard *.h)
OBJECTS		:= $(SOURCES:$%.c=$%.o)

prefix		:= /usr/local
servicedir      := /etc/systemd/system
RM		:= rm -f
INSTALL		:= install -m 4755
INSTALLDIR	:= install -d -m 755

# linking Objects
$(TARGET): $(OBJECTS)
	@$(LINKER) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo "Made: $@"


# compiling command
$(OBJECTS): %.o : %.c $(INCLUDES)
	@$(CC) $(CFLAGS) $(WFLAGS) $(DEBUGFLAG) $< -o $@
	@echo "Compiled: $@"

all:	$(TARGET)

debug:
	$(MAKE) $(MAKEFILE) DEBUGFLAG="-g -D DEBUG"
	@echo " "
	@echo "$(TARGET) Make Debug Complete"
	@echo " "

install:
	@$(MAKE) --no-print-directory
	@$(INSTALLDIR) $(DESTDIR)$(prefix)/bin
	@$(INSTALL) $(TARGET) $(DESTDIR)$(prefix)/bin
	@cp $(TARGET).service $(servicedir)/
	@systemctl daemon-reload
	@systemctl enable $(TARGET).service
	@systemctl restart $(TARGET).service
	@echo "$(TARGET) Install Complete"

clean:
	@$(RM) $(OBJECTS)
	@$(RM) $(TARGET)
	@echo "$(TARGET) Clean Complete"

uninstall:
	@$(RM) $(DESTDIR)$(prefix)/bin/$(TARGET)
	@systemctl stop $(TARGET).service
	@systemctl disable $(TARGET).service
	@rm $(servicedir)/$(TARGET).service
	@echo "$(TARGET) Uninstall Complete"

