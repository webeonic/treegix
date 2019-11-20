all: $(PREBUILD) $(PROGNAME) $(POSTBUILD)

clean: $(POSTCLEAN)
# break this down because del has limit for command-line args length (NMAKE : fatal error U1077: 'del' : return code '0x1')
	del $(OBJS) 2>NUL
	del $(RESOURCE_RES) $(MESSAGES_H) $(PROGNAME) messages.rc MSG00001.bin $(TARGETNAME).$(TARGETEXT).manifest \
		$(PDBS) $(TARGETNAME).$(TARGETEXT).idb 2>NUL

$(PROGNAME): $(MESSAGES_H) $(OBJS) $(RESOURCE_RES)
	$(LINKER) $(OBJS) $(RESOURCE_RES) $(LFLAGS) /OUT:$(PROGNAME) $(LIBS)
	$(MT) /outputresource:"$(PROGNAME);#1" /manifest $(TARGETNAME).$(TARGETEXT).manifest /nologo

$(MESSAGES_H): messages.mc
	$(MC) -U -h ".\\" -r ".\\" messages.mc

$(RESOURCE_RES): $(RESOURCE_RC) $(RESOURCE_H) $(DESC_H)
	$(RC) $(RFLAGS) /d "NDEBUG" /d "_VC80_UPGRADE=0x0600" /l 0x419 /fo"$(RESOURCE_RES)" $(RESOURCE_RC)

{..\..\..\src\libs\trxalgo}.c{..\..\..\src\libs\trxalgo}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxcommon}.c{..\..\..\src\libs\trxcommon}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxcomms}.c{..\..\..\src\libs\trxcomms}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxcompress}.c{..\..\..\src\libs\trxcompress}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxconf}.c{..\..\..\src\libs\trxconf}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxcrypto}.c{..\..\..\src\libs\trxcrypto}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxjson}.c{..\..\..\src\libs\trxjson}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxlog}.c{..\..\..\src\libs\trxlog}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxregexp}.c{..\..\..\src\libs\trxregexp}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxsys}.c{..\..\..\src\libs\trxsys}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxexec}.c{..\..\..\src\libs\trxexec}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxhttp}.c{..\..\..\src\libs\trxhttp}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxsysinfo}.c{..\..\..\src\libs\trxsysinfo}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxsysinfo\agent}.c{..\..\..\src\libs\trxsysinfo\agent}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxsysinfo\simple}.c{..\..\..\src\libs\trxsysinfo\simple}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxsysinfo\common}.c{..\..\..\src\libs\trxsysinfo\common}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxsysinfo\win32}.c{..\..\..\src\libs\trxsysinfo\win32}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\libs\trxsysinfo\win32}.cpp{..\..\..\src\libs\trxsysinfo\win32}.o:
	$(CC) $? /Fo"$@" $(CPPFLAGS)

{..\..\..\src\libs\trxwin32}.c{..\..\..\src\libs\trxwin32}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{..\..\..\src\treegix_agent\logfiles}.c{..\..\..\src\treegix_agent\logfiles}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)

{$(PROJECTDIR)}.c{$(PROJECTDIR)}.o:
	$(CC) $? /Fo"$@" $(CFLAGS)
