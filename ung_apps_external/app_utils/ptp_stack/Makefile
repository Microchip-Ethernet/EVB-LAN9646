# Linux network programming samples makefile


GCC ?= $(CROSS_COMPILE)gcc

ifeq (-gnueabi-, $(findstring -gnueabi-, $(CROSS_COMPILE)))
	CFLAGS += -march=armv4t
	LDFLAGS += -march=armv4t
endif

CFLAGS += -Wall -Wno-array-bounds -Wno-unused-variable -Wno-unused-function \
	-Wno-unused-but-set-variable -DLINUX_PTP

SOCKDIR = ./socket
OS = ./os

HEADERS = $(SOCKDIR)/wnp.h $(SOCKDIR)/inetfunc.h $(SOCKDIR)/iofunc.h \
	$(SOCKDIR)/wrapsock.h $(SOCKDIR)/wrapstdio.h \
	$(SOCKDIR)/wrapmcast.h $(SOCKDIR)/unpifi.h \
	$(OS)/config_store.h $(OS)/sw_support.h

STACK = ptp_clock.c ptp_clock.h ptp_port.c ptp_port.h \
	ptp_protocol.c ptp_protocol.h ptp_forward.c ptp_forward.h \
	ptp_filter.c ptp_filter.h ptp_signal.c ptp_servo.c ptp_servo.h \
	ptp_bmca.c ptp_bmca.h ptp_msg.c ptp_msg.h ptp_ptu.c ptp_ptu.h \
	lan937x_ptp_api.c lan937x_ptp_api.h lan937x_req.h \
	avtpdu_1722.c avtpdu_1722.h ptp_dbg.h \
	ptp_stack.c ptp_config.c ptp_config.h ptp_manage.c ptp_sock.c

INCDIR  = -I$(SOCKDIR) -I$(OS)


%.o: %.c
	@$(GCC) -O2 $(CFLAGS) -c $<

%.o: $(SOCKDIR)/%.c
	@$(GCC) -O2 $(CFLAGS) -c $<

%.o: $(OS)/%.c
	@$(GCC) -O2 $(INCDIR) $(CFLAGS) -c $<


OBJS = error.o inetfunc.o iofunc.o wrapsock.o wrapstdio.o ip.o wrapmcast.o
OBJS += sockntop.o unpifi.o signalthread.o
OBJS += config_store.o sw_support.o

EXE=ptp_stack

all: $(EXE)

error.o		: $(SOCKDIR)/error.c
inetfunc.o	: $(SOCKDIR)/inetfunc.c $(HEADERS)
iofunc.o	: $(SOCKDIR)/iofunc.c $(HEADERS)
sockntop.o	: $(SOCKDIR)/iofunc.c $(HEADERS)
wrapmcast.o	: $(SOCKDIR)/wrapmcast.c $(HEADERS)
wrapsock.o	: $(SOCKDIR)/wrapsock.c $(HEADERS)
wrapstdio.o	: $(SOCKDIR)/wrapstdio.c $(HEADERS)
ip.o		: $(SOCKDIR)/ip.c $(HEADERS)
unpifi.o        : $(SOCKDIR)/unpifi.c $(HEADERS)
wrapthread.o    : $(SOCKDIR)/wrapthread.c $(HEADERS)
signalthread.o  : $(SOCKDIR)/signalthread.c $(HEADERS)

config_store.o  : $(OS)/config_store.c $(HEADERS)
sw_support.o    : $(OS)/sw_support.c $(HEADERS)


ptp_stack: ptp_main.c $(STACK) $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

clean:
	@rm -f *.o

distclean: clean
	@rm -f $(EXE)
