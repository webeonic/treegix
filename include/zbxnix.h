

#ifndef TREEGIX_ZBXNIX_H
#define TREEGIX_ZBXNIX_H

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
int	zbx_coredump_disable(void);
#endif

#endif	/* TREEGIX_ZBXNIX_H */
