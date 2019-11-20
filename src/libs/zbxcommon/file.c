

#include "common.h"

#if defined(_WINDOWS)
int	__trx_open(const char *pathname, int flags)
{
	int	ret;
	wchar_t	*wpathname;

	wpathname = trx_utf8_to_unicode(pathname);
	ret = _wopen(wpathname, flags);
	trx_free(wpathname);

	return ret;
}
#endif

void	find_cr_lf_szbyte(const char *encoding, const char **cr, const char **lf, size_t *szbyte)
{
	/* default is single-byte character set */
	*cr = "\r";
	*lf = "\n";
	*szbyte = 1;

	if ('\0' != *encoding)
	{
		if (0 == strcasecmp(encoding, "UNICODE") || 0 == strcasecmp(encoding, "UNICODELITTLE") ||
				0 == strcasecmp(encoding, "UTF-16") || 0 == strcasecmp(encoding, "UTF-16LE") ||
				0 == strcasecmp(encoding, "UTF16") || 0 == strcasecmp(encoding, "UTF16LE") ||
				0 == strcasecmp(encoding, "UCS-2") || 0 == strcasecmp(encoding, "UCS-2LE"))
		{
			*cr = "\r\0";
			*lf = "\n\0";
			*szbyte = 2;
		}
		else if (0 == strcasecmp(encoding, "UNICODEBIG") || 0 == strcasecmp(encoding, "UNICODEFFFE") ||
				0 == strcasecmp(encoding, "UTF-16BE") || 0 == strcasecmp(encoding, "UTF16BE") ||
				0 == strcasecmp(encoding, "UCS-2BE"))
		{
			*cr = "\0\r";
			*lf = "\0\n";
			*szbyte = 2;
		}
		else if (0 == strcasecmp(encoding, "UTF-32") || 0 == strcasecmp(encoding, "UTF-32LE") ||
				0 == strcasecmp(encoding, "UTF32") || 0 == strcasecmp(encoding, "UTF32LE"))
		{
			*cr = "\r\0\0\0";
			*lf = "\n\0\0\0";
			*szbyte = 4;
		}
		else if (0 == strcasecmp(encoding, "UTF-32BE") || 0 == strcasecmp(encoding, "UTF32BE"))
		{
			*cr = "\0\0\0\r";
			*lf = "\0\0\0\n";
			*szbyte = 4;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_read                                                         *
 *                                                                            *
 * Purpose: Read one text line from a file descriptor into buffer             *
 *                                                                            *
 * Parameters: fd       - [IN] file descriptor to read from                   *
 *             buf      - [IN] buffer to read into                            *
 *             count    - [IN] buffer size in bytes                           *
 *             encoding - [IN] pointer to a text string describing encoding.  *
 *                        See function find_cr_lf_szbyte() for supported      *
 *                        encodings.                                          *
 *                        "" (empty string) means a single-byte character set.*
 *                                                                            *
 * Return value: On success, the number of bytes read is returned (0 (zero)   *
 *               indicates end of file).                                      *
 *               On error, -1 is returned and errno is set appropriately.     *
 *                                                                            *
 * Comments: Reading stops after a newline. If the newline is read, it is     *
 *           stored into the buffer.                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_read(int fd, char *buf, size_t count, const char *encoding)
{
	size_t		i, szbyte, nbytes;
	const char	*cr, *lf;
	trx_offset_t	offset;

	if ((trx_offset_t)-1 == (offset = trx_lseek(fd, 0, SEEK_CUR)))
		return -1;

	if (0 >= (nbytes = read(fd, buf, (unsigned int)count)))
		return (int)nbytes;

	find_cr_lf_szbyte(encoding, &cr, &lf, &szbyte);

	for (i = 0; i <= nbytes - szbyte; i += szbyte)
	{
		if (0 == memcmp(&buf[i], lf, szbyte))	/* LF (Unix) */
		{
			i += szbyte;
			break;
		}

		if (0 == memcmp(&buf[i], cr, szbyte))	/* CR (Mac) */
		{
			/* CR+LF (Windows) ? */
			if (i < nbytes - szbyte && 0 == memcmp(&buf[i + szbyte], lf, szbyte))
				i += szbyte;

			i += szbyte;
			break;
		}
	}

	if ((trx_offset_t)-1 == trx_lseek(fd, offset + (trx_offset_t)i, SEEK_SET))
		return -1;

	return (int)i;
}

int	trx_is_regular_file(const char *path)
{
	trx_stat_t	st;

	if (0 == trx_stat(path, &st) && 0 != S_ISREG(st.st_mode))
		return SUCCEED;

	return FAIL;
}
