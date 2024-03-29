# Linux network programming samples makefile


GCC ?= $(CROSS_COMPILE)gcc

ifeq (-gnueabi-, $(findstring -gnueabi-, $(CROSS_COMPILE)))
	CFLAGS += -march=armv4t
	LDFLAGS += -march=armv4t
endif

CFLAGS += -Wall -Wno-array-bounds -Wno-unused-variable -Wno-unused-function \
	-Wno-unused-but-set-variable

SOCKDIR = ../socket

HEADERS = $(SOCKDIR)/wnp.h $(SOCKDIR)/inetfunc.h $(SOCKDIR)/iofunc.h \
	$(SOCKDIR)/wrapsock.h $(SOCKDIR)/wrapstdio.h \
	$(SOCKDIR)/wrapmcast.h $(SOCKDIR)/unpifi.h

INCDIR  = -I$(SOCKDIR)


%.o: %.c
	@$(GCC) -O2 $(CFLAGS) -c $<

%.o: $(SOCKDIR)/%.c
	@$(GCC) -O2 $(CFLAGS) -c $<


OBJS = error.o inetfunc.o iofunc.o wrapsock.o wrapstdio.o ip.o wrapmcast.o
OBJS += sockntop.o unpifi.o

EXE=ptp prifinfo rstptest acltest authtest nettest iba kszsw ksz dlr dlr_daemon
EXE+=echoclient echoudpclient echotcpserver

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


ptp: ptp.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

prifinfo: prifinfo.c $(HEADERS) $(OBJS)
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS)

rstptest: rstptest.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

acltest: acltest.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

authtest: authtest.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

nettest: nettest.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

iba: iba.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

kszsw: kszsw.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

ksz: ksz.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

dlr: dlr.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

dlr_daemon: dlr_daemon.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

echoclient: echoclient.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

echoudpclient: echoudpclient.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

echotcpserver: echotcpserver.c $(HEADERS) $(OBJS) wrapthread.o
	$(GCC) $(INCDIR) -O2 $(CFLAGS) -o $@ $< $(OBJS) wrapthread.o -pthread

clean:
	@rm -f *.o

distclean: clean
	@rm -f $(EXE)
