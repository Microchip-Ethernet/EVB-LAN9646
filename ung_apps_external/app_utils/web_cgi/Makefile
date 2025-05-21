

EXE=web.cgi

all: $(EXE)

web.cgi: web_cgi.c
	$(CROSS_COMPILE)gcc -o web.cgi $(CFLAGS) web_cgi.c

clean:
	$(RM) $(EXE)

install: $(EXE)
	cp -r $(EXE) $(DESTDIR)

