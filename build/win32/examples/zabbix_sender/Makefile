CC = cl.exe /nologo
LINKER = link.exe /NOLOGO

CFLAGS = /I ..\..\..\..\bin\win32\dev /c
LFLAGS = /LIBPATH:..\..\..\..\bin\win32\dev /DEFAULTLIB:treegix_sender.lib

PROGNAME = sender.exe
OBJS = sender.obj
LIBS = treegix_sender.lib ws2_32.lib

all: $(PROGNAME)

clean:
	del $(OBJS) $(PROGNAME)

sender.exe: $(OBJS)
	$(LINKER) $(OBJS) /OUT:$(PROGNAME) $(LFLAGS) $(LIBS)

sender.obj: sender.c
	$(CC) sender.c $(CFLAGS)
