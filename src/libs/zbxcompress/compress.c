

#include "common.h"
#include "log.h"
#include "trxcompress.h"

#ifdef HAVE_ZLIB
#include "zlib.h"

#define TRX_COMPRESS_STRERROR_LEN	512

static int	trx_zlib_errno = 0;

/******************************************************************************
 *                                                                            *
 * Function: trx_compress_strerror                                            *
 *                                                                            *
 * Purpose: returns last conversion error message                             *
 *                                                                            *
 ******************************************************************************/
const char	*trx_compress_strerror(void)
{
	static char	message[TRX_COMPRESS_STRERROR_LEN];

	switch (trx_zlib_errno)
	{
		case Z_ERRNO:
			trx_strlcpy(message, trx_strerror(errno), sizeof(message));
			break;
		case Z_MEM_ERROR:
			trx_strlcpy(message, "not enough memory", sizeof(message));
			break;
		case Z_BUF_ERROR:
			trx_strlcpy(message, "not enough space in output buffer", sizeof(message));
			break;
		case Z_DATA_ERROR:
			trx_strlcpy(message, "corrupted input data", sizeof(message));
			break;
		default:
			trx_snprintf(message, sizeof(message), "unknown error (%d)", trx_zlib_errno);
			break;
	}

	return message;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_compress                                                     *
 *                                                                            *
 * Purpose: compress data                                                     *
 *                                                                            *
 * Parameters: in       - [IN] the data to compress                           *
 *             size_in  - [IN] the input data size                            *
 *             out      - [OUT] the compressed data                           *
 *             size_out - [OUT] the compressed data size                      *
 *                                                                            *
 * Return value: SUCCEED - the data was compressed successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: In the case of success the output buffer must be freed by the    *
 *           caller.                                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_compress(const char *in, size_t size_in, char **out, size_t *size_out)
{
	Bytef	*buf;
	uLongf	buf_size;

	buf_size = compressBound(size_in);
	buf = (Bytef *)trx_malloc(NULL, buf_size);

	if (Z_OK != (trx_zlib_errno = compress(buf, &buf_size, (const Bytef *)in, size_in)))
	{
		trx_free(buf);
		return FAIL;
	}

	*out = (char *)buf;
	*size_out = buf_size;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_uncompress                                                   *
 *                                                                            *
 * Purpose: uncompress data                                                   *
 *                                                                            *
 * Parameters: in       - [IN] the data to uncompress                         *
 *             size_in  - [IN] the input data size                            *
 *             out      - [OUT] the uncompressed data                         *
 *             size_out - [IN/OUT] the buffer and uncompressed data size      *
 *                                                                            *
 * Return value: SUCCEED - the data was uncompressed successfully             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_uncompress(const char *in, size_t size_in, char *out, size_t *size_out)
{
	uLongf	size_o = *size_out;

	if (Z_OK != (trx_zlib_errno = uncompress((Bytef *)out, &size_o, (const Bytef *)in, size_in)))
		return FAIL;

	*size_out = size_o;

	return SUCCEED;
}

#else

int trx_compress(const char *in, size_t size_in, char **out, size_t *size_out)
{
	TRX_UNUSED(in);
	TRX_UNUSED(size_in);
	TRX_UNUSED(out);
	TRX_UNUSED(size_out);
	return FAIL;
}

int trx_uncompress(const char *in, size_t size_in, char *out, size_t *size_out)
{
	TRX_UNUSED(in);
	TRX_UNUSED(size_in);
	TRX_UNUSED(out);
	TRX_UNUSED(size_out);
	return FAIL;
}

const char	*trx_compress_strerror(void)
{
	return "";
}

#endif
