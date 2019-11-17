

package com.treegix.gateway;

import java.util.Formatter;

class TreegixException extends Exception
{
	TreegixException(String message)
	{
		super(message);
	}

	TreegixException(String message, Object... args)
	{
		this(new Formatter().format(message, args).toString());
	}

	TreegixException(String message, Throwable cause)
	{
		super(message, cause);
	}

	TreegixException(Throwable cause)
	{
		super(cause);
	}

	static Throwable getRootCause(Throwable e)
	{
		Throwable cause = null;
		Throwable result = e;

		while ((null != (cause = result.getCause())) && (result != cause))
			result = cause;

		return result;
	}
	
	static String getRootCauseMessage(Throwable e)
	{
		if (e != null)
			return getRootCause(e).getMessage();

		return null;
	}
}
