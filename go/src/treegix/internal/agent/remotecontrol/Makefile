# Makefile.in generated by automake 1.15.1 from Makefile.am.
# go/src/treegix/internal/agent/remotecontrol/Makefile.  Generated from Makefile.in by configure.

# Copyright (C) 1994-2017 Free Software Foundation, Inc.

# This Makefile.in is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.



am__is_gnu_make = { \
  if test -z '$(MAKELEVEL)'; then \
    false; \
  elif test -n '$(MAKE_HOST)'; then \
    true; \
  elif test -n '$(MAKE_VERSION)' && test -n '$(CURDIR)'; then \
    true; \
  else \
    false; \
  fi; \
}
am__make_running_with_option = \
  case $${target_option-} in \
      ?) ;; \
      *) echo "am__make_running_with_option: internal error: invalid" \
              "target option '$${target_option-}' specified" >&2; \
         exit 1;; \
  esac; \
  has_opt=no; \
  sane_makeflags=$$MAKEFLAGS; \
  if $(am__is_gnu_make); then \
    sane_makeflags=$$MFLAGS; \
  else \
    case $$MAKEFLAGS in \
      *\\[\ \	]*) \
        bs=\\; \
        sane_makeflags=`printf '%s\n' "$$MAKEFLAGS" \
          | sed "s/$$bs$$bs[$$bs $$bs	]*//g"`;; \
    esac; \
  fi; \
  skip_next=no; \
  strip_trailopt () \
  { \
    flg=`printf '%s\n' "$$flg" | sed "s/$$1.*$$//"`; \
  }; \
  for flg in $$sane_makeflags; do \
    test $$skip_next = yes && { skip_next=no; continue; }; \
    case $$flg in \
      *=*|--*) continue;; \
        -*I) strip_trailopt 'I'; skip_next=yes;; \
      -*I?*) strip_trailopt 'I';; \
        -*O) strip_trailopt 'O'; skip_next=yes;; \
      -*O?*) strip_trailopt 'O';; \
        -*l) strip_trailopt 'l'; skip_next=yes;; \
      -*l?*) strip_trailopt 'l';; \
      -[dEDm]) skip_next=yes;; \
      -[JT]) skip_next=yes;; \
    esac; \
    case $$flg in \
      *$$target_option*) has_opt=yes; break;; \
    esac; \
  done; \
  test $$has_opt = yes
am__make_dryrun = (target_option=n; $(am__make_running_with_option))
am__make_keepgoing = (target_option=k; $(am__make_running_with_option))
pkgdatadir = $(datadir)/treegix
pkgincludedir = $(includedir)/treegix
pkglibdir = $(libdir)/treegix
pkglibexecdir = $(libexecdir)/treegix
am__cd = CDPATH="$${ZSH_VERSION+.}$(PATH_SEPARATOR)" && cd
install_sh_DATA = $(install_sh) -c -m 644
install_sh_PROGRAM = $(install_sh) -c
install_sh_SCRIPT = $(install_sh) -c
INSTALL_HEADER = $(INSTALL_DATA)
transform = $(program_transform_name)
NORMAL_INSTALL = :
PRE_INSTALL = :
POST_INSTALL = :
NORMAL_UNINSTALL = :
PRE_UNINSTALL = :
POST_UNINSTALL = :
build_triplet = x86_64-pc-linux-gnu
host_triplet = x86_64-pc-linux-gnu
subdir = go/src/treegix/internal/agent/remotecontrol
ACLOCAL_M4 = $(top_srcdir)/aclocal.m4
am__aclocal_m4_deps = $(top_srcdir)/m4/ax_lib_ibm_db2.m4 \
	$(top_srcdir)/m4/ax_lib_mysql.m4 \
	$(top_srcdir)/m4/ax_lib_oracle_oci.m4 \
	$(top_srcdir)/m4/ax_lib_postgresql.m4 \
	$(top_srcdir)/m4/ax_lib_sqlite3.m4 $(top_srcdir)/m4/iconv.m4 \
	$(top_srcdir)/m4/ldap.m4 $(top_srcdir)/m4/libcurl.m4 \
	$(top_srcdir)/m4/libevent.m4 $(top_srcdir)/m4/libgnutls.m4 \
	$(top_srcdir)/m4/libmbedtls.m4 $(top_srcdir)/m4/libopenssl.m4 \
	$(top_srcdir)/m4/libssh2.m4 $(top_srcdir)/m4/libunixodbc.m4 \
	$(top_srcdir)/m4/libxml2.m4 $(top_srcdir)/m4/netsnmp.m4 \
	$(top_srcdir)/m4/openipmi.m4 $(top_srcdir)/m4/pcre.m4 \
	$(top_srcdir)/m4/pthread.m4 $(top_srcdir)/m4/resolv.m4 \
	$(top_srcdir)/m4/zlib.m4 $(top_srcdir)/configure.ac
am__configure_deps = $(am__aclocal_m4_deps) $(CONFIGURE_DEPENDENCIES) \
	$(ACLOCAL_M4)
DIST_COMMON = $(srcdir)/Makefile.am $(am__DIST_COMMON)
mkinstalldirs = $(install_sh) -d
CONFIG_HEADER = $(top_builddir)/include/config.h
CONFIG_CLEAN_FILES =
CONFIG_CLEAN_VPATH_FILES =
AM_V_P = $(am__v_P_$(V))
am__v_P_ = $(am__v_P_$(AM_DEFAULT_VERBOSITY))
am__v_P_0 = false
am__v_P_1 = :
AM_V_GEN = $(am__v_GEN_$(V))
am__v_GEN_ = $(am__v_GEN_$(AM_DEFAULT_VERBOSITY))
am__v_GEN_0 = @echo "  GEN     " $@;
am__v_GEN_1 = 
AM_V_at = $(am__v_at_$(V))
am__v_at_ = $(am__v_at_$(AM_DEFAULT_VERBOSITY))
am__v_at_0 = @
am__v_at_1 = 
SOURCES =
DIST_SOURCES =
am__can_run_installinfo = \
  case $$AM_UPDATE_INFO_DIR in \
    n|no|NO) false;; \
    *) (install-info --version) >/dev/null 2>&1;; \
  esac
am__tagged_files = $(HEADERS) $(SOURCES) $(TAGS_FILES) $(LISP)
am__DIST_COMMON = $(srcdir)/Makefile.in
DISTFILES = $(DIST_COMMON) $(DIST_SOURCES) $(TEXINFOS) $(EXTRA_DIST)
ACLOCAL = ${SHELL} /home/vs/PhpstormProjects/treegix/missing aclocal-1.15
AGENT2_CONFIG_FILE = ${prefix}/etc/treegix_agent2.conf
AGENT_CONFIG_FILE = ${prefix}/etc/treegix_agentd.conf
AGENT_LDFLAGS =      
AGENT_LIBS =  -lz -lpthread   
ALERT_SCRIPTS_PATH = ${datarootdir}/treegix/alertscripts
AMTAR = $${TAR-tar}
AM_DEFAULT_VERBOSITY = 1
ARCH = linux
AUTOCONF = ${SHELL} /home/vs/PhpstormProjects/treegix/missing autoconf
AUTOHEADER = ${SHELL} /home/vs/PhpstormProjects/treegix/missing autoheader
AUTOMAKE = ${SHELL} /home/vs/PhpstormProjects/treegix/missing automake-1.15
AWK = mawk
CC = cc
CCDEPMODE = depmode=gcc3
CFLAGS =  -g -O2 
CGO_LDFLAGS = -Wl,--start-group 	${abs_top_builddir}/src/treegix_agent/logfiles/libtrxlogfiles.a 	${abs_top_builddir}/src/libs/trxcomms/libtrxcomms.a 	${abs_top_builddir}/src/libs/trxcommon/libtrxcommon.a 	${abs_top_builddir}/src/libs/trxcrypto/libtrxcrypto.a 	${abs_top_builddir}/src/libs/trxsys/libtrxsys.a 	${abs_top_builddir}/src/libs/trxnix/libtrxnix.a 	${abs_top_builddir}/src/libs/trxconf/libtrxconf.a 	${abs_top_builddir}/src/libs/trxhttp/libtrxhttp.a 	${abs_top_builddir}/src/libs/trxcompress/libtrxcompress.a 	${abs_top_builddir}/src/libs/trxregexp/libtrxregexp.a 	${abs_top_builddir}/src/libs/trxsysinfo/libtrxagentsysinfo.a 	${abs_top_builddir}/src/libs/trxsysinfo/common/libcommonsysinfo.a 	${abs_top_builddir}/src/libs/trxsysinfo/simple/libsimplesysinfo.a 	${abs_top_builddir}/src/libs/trxsysinfo/linux/libspechostnamesysinfo.a 	${abs_top_builddir}/src/libs/trxsysinfo/linux/libspecsysinfo.a 	${abs_top_builddir}/src/libs/trxexec/libtrxexec.a 	${abs_top_builddir}/src/libs/trxalgo/libtrxalgo.a 	${abs_top_builddir}/src/libs/trxjson/libtrxjson.a 	-lm -ldl  -lresolv -lpcre   -lz -lpthread    	-Wl,--end-group
CPP = cc -E
CPPFLAGS = 
CURL_SSL_CERT_LOCATION = ${datarootdir}/treegix/ssl/certs
CURL_SSL_KEY_LOCATION = ${datarootdir}/treegix/ssl/keys
CYGPATH_W = echo
DB_CFLAGS =    -I/usr/include/postgresql 
DB_LDFLAGS =    -L/usr/lib/x86_64-linux-gnu 
DB_LIBS =    -lpq 
DEFS = -DHAVE_CONFIG_H
DEPDIR = .deps
ECHO_C = 
ECHO_N = -n
ECHO_T = 
EGREP = /bin/grep -E
EXEEXT = 
EXTERNAL_SCRIPTS_PATH = ${datarootdir}/treegix/externalscripts
GNUTLS_CFLAGS = 
GNUTLS_LDFLAGS = 
GNUTLS_LIBS = 
GO = 
GOBIN = ${exec_prefix}/sbin
GOCMD = go
GOPATH = /home/vs/PhpstormProjects/treegix/go
GOWORKDIR = /home/vs/PhpstormProjects/treegix/go/src/treegix
GREP = /bin/grep
ICONV_CFLAGS = 
ICONV_LDFLAGS = 
INSTALL = /usr/bin/install -c
INSTALL_DATA = ${INSTALL} -m 644
INSTALL_PROGRAM = ${INSTALL}
INSTALL_SCRIPT = ${INSTALL}
INSTALL_STRIP_PROGRAM = $(install_sh) -c -s
JAR = 
JAVAC = 
LDAP_CPPFLAGS = 
LDAP_LDFLAGS = 
LDAP_LIBS = 
LDFLAGS = -rdynamic   
LIBCURL_CFLAGS = 
LIBCURL_LDFLAGS = 
LIBCURL_LIBS = 
LIBEVENT_CFLAGS = 
LIBEVENT_LDFLAGS = 
LIBEVENT_LIBS = -levent
LIBOBJS = 
LIBPCRE_CFLAGS = 
LIBPCRE_LDFLAGS = 
LIBPCRE_LIBS = -lpcre
LIBPTHREAD_CFLAGS = 
LIBPTHREAD_LDFLAGS = 
LIBPTHREAD_LIBS = -lpthread
LIBS = -lm -ldl  -lresolv -lpcre 
LIBXML2_CFLAGS = 
LIBXML2_CONFIG = no
LIBXML2_LDFLAGS = 
LIBXML2_LIBS = 
LIBXML2_VERSION = 
LOAD_MODULE_PATH = ${exec_prefix}/lib/modules
LTLIBOBJS = 
MAKEINFO = ${SHELL} /home/vs/PhpstormProjects/treegix/missing makeinfo
MBEDTLS_CFLAGS = 
MBEDTLS_LDFLAGS = 
MBEDTLS_LIBS = 
MKDIR_P = /bin/mkdir -p
MYSQL_CFLAGS = 
MYSQL_CONFIG = no
MYSQL_LDFLAGS = 
MYSQL_LIBS = 
MYSQL_VERSION = 
OBJEXT = o
ODBC_CONFIG = 
OPENIPMI_CFLAGS = 
OPENIPMI_LDFLAGS = 
OPENIPMI_LIBS = 
OPENSSL_CFLAGS = 
OPENSSL_LDFLAGS = 
OPENSSL_LIBS = 
ORACLE_OCI_CFLAGS = 
ORACLE_OCI_LDFLAGS = 
ORACLE_OCI_LIBS = 
ORACLE_OCI_VERSION = 
PACKAGE = treegix
PACKAGE_BUGREPORT = 
PACKAGE_NAME = Treegix
PACKAGE_STRING = Treegix 4.4.1
PACKAGE_TARNAME = treegix
PACKAGE_URL = 
PACKAGE_VERSION = 4.4.1
PATH_SEPARATOR = :
PG_CONFIG = /usr/bin/pg_config
POSTGRESQL_CFLAGS = -I/usr/include/postgresql
POSTGRESQL_LDFLAGS = -L/usr/lib/x86_64-linux-gnu
POSTGRESQL_LIBS = -lpq
POSTGRESQL_VERSION = 10.10
PROXY_CONFIG_FILE = ${prefix}/etc/treegix_proxy.conf
PROXY_LDFLAGS =     -L/usr/lib/x86_64-linux-gnu            
PROXY_LIBS =     -lpq       -lz -lpthread -levent   
RANLIB = ranlib
RESOLV_LIBS = -lresolv
SENDER_LDFLAGS =    
SENDER_LIBS =  -lz -lpthread 
SERVER_CONFIG_FILE = ${prefix}/etc/treegix_server.conf
SERVER_LDFLAGS =     -L/usr/lib/x86_64-linux-gnu            
SERVER_LIBS =     -lpq       -lz -lpthread -levent   
SET_MAKE = 
SHELL = /bin/bash
SNMP_CFLAGS = 
SNMP_LDFLAGS = 
SNMP_LIBS = 
SQLITE3_CPPFLAGS = 
SQLITE3_LDFLAGS = 
SQLITE3_LIBS = 
SQLITE3_VERSION = 
SSH2_CFLAGS = 
SSH2_LDFLAGS = 
SSH2_LIBS = 
STRIP = 
TLS_CFLAGS = 
UNIXODBC_CFLAGS = 
UNIXODBC_LDFLAGS = 
UNIXODBC_LIBS = 
VERSION = 4.4.1
TRXGET_LDFLAGS =
TRXGET_LIBS =  -lz -lpthread
ZLIB_CFLAGS = 
ZLIB_LDFLAGS = 
ZLIB_LIBS = -lz
_libcurl_config = no
_libnetsnmp_config = no
abs_builddir = /home/vs/PhpstormProjects/treegix/go/src/treegix/internal/agent/remotecontrol
abs_srcdir = /home/vs/PhpstormProjects/treegix/go/src/treegix/internal/agent/remotecontrol
abs_top_builddir = /home/vs/PhpstormProjects/treegix
abs_top_srcdir = /home/vs/PhpstormProjects/treegix
ac_ct_CC = cc
am__include = include
am__leading_dot = .
am__quote = 
am__tar = $${TAR-tar} chof - "$$tardir"
am__untar = $${TAR-tar} xf -
bindir = ${exec_prefix}/bin
build = x86_64-pc-linux-gnu
build_alias = 
build_cpu = x86_64
build_os = linux-gnu
build_vendor = pc
builddir = .
datadir = ${datarootdir}
datarootdir = ${prefix}/share
docdir = ${datarootdir}/doc/${PACKAGE_TARNAME}
dvidir = ${docdir}
exec_prefix = ${prefix}
host = x86_64-pc-linux-gnu
host_alias = 
host_cpu = x86_64
host_os = linux-gnu
host_vendor = pc
htmldir = ${docdir}
includedir = ${prefix}/include
infodir = ${datarootdir}/info
install_sh = ${SHELL} /home/vs/PhpstormProjects/treegix/install-sh
libdir = ${exec_prefix}/lib
libexecdir = ${exec_prefix}/libexec
localedir = ${datarootdir}/locale
localstatedir = ${prefix}/var
mandir = ${datarootdir}/man
mkdir_p = $(MKDIR_P)
oldincludedir = /usr/include
pdfdir = ${docdir}
prefix = /usr/local
program_transform_name = s,x,x,
psdir = ${docdir}
runstatedir = ${localstatedir}/run
sbindir = ${exec_prefix}/sbin
sharedstatedir = ${prefix}/com
srcdir = .
sysconfdir = ${prefix}/etc
target_alias = 
top_build_prefix = ../../../../../../
top_builddir = ../../../../../..
top_srcdir = ../../../../../..
all: all-am

.SUFFIXES:
$(srcdir)/Makefile.in:  $(srcdir)/Makefile.am  $(am__configure_deps)
	@for dep in $?; do \
	  case '$(am__configure_deps)' in \
	    *$$dep*) \
	      ( cd $(top_builddir) && $(MAKE) $(AM_MAKEFLAGS) am--refresh ) \
	        && { if test -f $@; then exit 0; else break; fi; }; \
	      exit 1;; \
	  esac; \
	done; \
	echo ' cd $(top_srcdir) && $(AUTOMAKE) --gnu go/src/treegix/internal/agent/remotecontrol/Makefile'; \
	$(am__cd) $(top_srcdir) && \
	  $(AUTOMAKE) --gnu go/src/treegix/internal/agent/remotecontrol/Makefile
Makefile: $(srcdir)/Makefile.in $(top_builddir)/config.status
	@case '$?' in \
	  *config.status*) \
	    cd $(top_builddir) && $(MAKE) $(AM_MAKEFLAGS) am--refresh;; \
	  *) \
	    echo ' cd $(top_builddir) && $(SHELL) ./config.status $(subdir)/$@ $(am__depfiles_maybe)'; \
	    cd $(top_builddir) && $(SHELL) ./config.status $(subdir)/$@ $(am__depfiles_maybe);; \
	esac;

$(top_builddir)/config.status: $(top_srcdir)/configure $(CONFIG_STATUS_DEPENDENCIES)
	cd $(top_builddir) && $(MAKE) $(AM_MAKEFLAGS) am--refresh

$(top_srcdir)/configure:  $(am__configure_deps)
	cd $(top_builddir) && $(MAKE) $(AM_MAKEFLAGS) am--refresh
$(ACLOCAL_M4):  $(am__aclocal_m4_deps)
	cd $(top_builddir) && $(MAKE) $(AM_MAKEFLAGS) am--refresh
$(am__aclocal_m4_deps):
tags TAGS:

ctags CTAGS:

cscope cscopelist:


distdir: $(DISTFILES)
	@srcdirstrip=`echo "$(srcdir)" | sed 's/[].[^$$\\*]/\\\\&/g'`; \
	topsrcdirstrip=`echo "$(top_srcdir)" | sed 's/[].[^$$\\*]/\\\\&/g'`; \
	list='$(DISTFILES)'; \
	  dist_files=`for file in $$list; do echo $$file; done | \
	  sed -e "s|^$$srcdirstrip/||;t" \
	      -e "s|^$$topsrcdirstrip/|$(top_builddir)/|;t"`; \
	case $$dist_files in \
	  */*) $(MKDIR_P) `echo "$$dist_files" | \
			   sed '/\//!d;s|^|$(distdir)/|;s,/[^/]*$$,,' | \
			   sort -u` ;; \
	esac; \
	for file in $$dist_files; do \
	  if test -f $$file || test -d $$file; then d=.; else d=$(srcdir); fi; \
	  if test -d $$d/$$file; then \
	    dir=`echo "/$$file" | sed -e 's,/[^/]*$$,,'`; \
	    if test -d "$(distdir)/$$file"; then \
	      find "$(distdir)/$$file" -type d ! -perm -700 -exec chmod u+rwx {} \;; \
	    fi; \
	    if test -d $(srcdir)/$$file && test $$d != $(srcdir); then \
	      cp -fpR $(srcdir)/$$file "$(distdir)$$dir" || exit 1; \
	      find "$(distdir)/$$file" -type d ! -perm -700 -exec chmod u+rwx {} \;; \
	    fi; \
	    cp -fpR $$d/$$file "$(distdir)$$dir" || exit 1; \
	  else \
	    test -f "$(distdir)/$$file" \
	    || cp -p $$d/$$file "$(distdir)/$$file" \
	    || exit 1; \
	  fi; \
	done
check-am: all-am
check: check-am
all-am: Makefile
installdirs:
install: install-am
install-exec: install-exec-am
install-data: install-data-am
uninstall: uninstall-am

install-am: all-am
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

installcheck: installcheck-am
install-strip:
	if test -z '$(STRIP)'; then \
	  $(MAKE) $(AM_MAKEFLAGS) INSTALL_PROGRAM="$(INSTALL_STRIP_PROGRAM)" \
	    install_sh_PROGRAM="$(INSTALL_STRIP_PROGRAM)" INSTALL_STRIP_FLAG=-s \
	      install; \
	else \
	  $(MAKE) $(AM_MAKEFLAGS) INSTALL_PROGRAM="$(INSTALL_STRIP_PROGRAM)" \
	    install_sh_PROGRAM="$(INSTALL_STRIP_PROGRAM)" INSTALL_STRIP_FLAG=-s \
	    "INSTALL_PROGRAM_ENV=STRIPPROG='$(STRIP)'" install; \
	fi
mostlyclean-generic:

clean-generic:

distclean-generic:
	-test -z "$(CONFIG_CLEAN_FILES)" || rm -f $(CONFIG_CLEAN_FILES)
	-test . = "$(srcdir)" || test -z "$(CONFIG_CLEAN_VPATH_FILES)" || rm -f $(CONFIG_CLEAN_VPATH_FILES)

maintainer-clean-generic:
	@echo "This command is intended for maintainers to use"
	@echo "it deletes files that may require special tools to rebuild."
clean-am: clean-generic mostlyclean-am

distclean: distclean-am
	-rm -f Makefile
distclean-am: clean-am distclean-generic

dvi: dvi-am

dvi-am:

html: html-am

html-am:

info: info-am

info-am:

install-data-am:

install-dvi: install-dvi-am

install-dvi-am:

install-exec-am:

install-html: install-html-am

install-html-am:

install-info: install-info-am

install-info-am:

install-man:

install-pdf: install-pdf-am

install-pdf-am:

install-ps: install-ps-am

install-ps-am:

installcheck-am:

maintainer-clean: maintainer-clean-am
	-rm -f Makefile
maintainer-clean-am: distclean-am maintainer-clean-generic

mostlyclean: mostlyclean-am

mostlyclean-am: mostlyclean-generic

pdf: pdf-am

pdf-am:

ps: ps-am

ps-am:

uninstall-am:

.MAKE: install-am install-strip

.PHONY: all all-am check check-am clean clean-generic cscopelist-am \
	ctags-am distclean distclean-generic distdir dvi dvi-am html \
	html-am info info-am install install-am install-data \
	install-data-am install-dvi install-dvi-am install-exec \
	install-exec-am install-html install-html-am install-info \
	install-info-am install-man install-pdf install-pdf-am \
	install-ps install-ps-am install-strip installcheck \
	installcheck-am installdirs maintainer-clean \
	maintainer-clean-generic mostlyclean mostlyclean-generic pdf \
	pdf-am ps ps-am tags-am uninstall uninstall-am

.PRECIOUS: Makefile


clean:
	go clean

# Tell versions [3.59,3.63) of GNU make to not export all variables.
# Otherwise a system limit (for SysV at least) may be exceeded.
.NOEXPORT:
