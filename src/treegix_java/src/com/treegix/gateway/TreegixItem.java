

package com.treegix.gateway;

import java.util.ArrayList;

class TreegixItem
{
	private String key = null;
	private String keyId = null;
	private ArrayList<String> args = null;

	TreegixItem(String key)
	{
		if (null == key)
			throw new IllegalArgumentException("key must not be null");

		int bracket = key.indexOf('[');

		if (-1 != bracket)
		{
			if (']' != key.charAt(key.length() - 1))
				throw new IllegalArgumentException("no terminating ']' in key: '" + key + "'");

			keyId = key.substring(0, bracket);
			args = parseArguments(key.substring(bracket + 1, key.length() - 1));
		}
		else
			keyId = key;

		if (0 == keyId.length())
			throw new IllegalArgumentException("key ID is empty in key: '" + key + "'");

		for (int i = 0; i < keyId.length(); i++)
			if (!isValidKeyIdChar(keyId.charAt(i)))
				throw new IllegalArgumentException("bad key ID char '" + keyId.charAt(i) + "' in key: '" + key + "'");

		this.key = key;
	}

	String getKey()
	{
		return key;
	}

	String getKeyId()
	{
		return keyId;
	}

	String getArgument(int index)
	{
		if (null == args || 1 > index || index > args.size())
			throw new IndexOutOfBoundsException("bad argument index for key '" + key + "': " + index);
		else
			return args.get(index - 1);
	}

	int getArgumentCount()
	{
		return null == args ? 0 : args.size();
	}

	private ArrayList<String> parseArguments(String keyArgs)
	{
		ArrayList<String> args = new ArrayList<String>();

		while (true)
		{
			if (0 == keyArgs.length())
			{
				args.add("");
				break;
			}
			else if (' ' == keyArgs.charAt(0))
			{
				keyArgs = keyArgs.substring(1);
			}
			else if ('"' == keyArgs.charAt(0))
			{
				int index = 1;

				while (index < keyArgs.length())
				{
					if ('"' == keyArgs.charAt(index) && '\\' != keyArgs.charAt(index - 1))
						break;
					else
						index++;
				}

				if (index == keyArgs.length())
					throw new IllegalArgumentException("quoted argument not terminated: '" + key + "'");

				args.add(keyArgs.substring(1, index).replace("\\\"", "\""));

				for (index++; index < keyArgs.length() && ' ' == keyArgs.charAt(index); index++)
					;

				if (index == keyArgs.length())
					break;

				if (',' != keyArgs.charAt(index))
					throw new IllegalArgumentException("quoted argument not followed by comma: '" + key + "'");

				keyArgs = keyArgs.substring(index + 1);
			}
			else
			{
				int index = 0;

				while (index < keyArgs.length() && ',' != keyArgs.charAt(index))
					index++;

				args.add(keyArgs.substring(0, index));

				if (index == keyArgs.length())
					break;

				keyArgs = keyArgs.substring(index + 1);
			}
		}

		return args;
	}

	private boolean isValidKeyIdChar(char ch)
	{
		return (('a' <= ch && ch <= 'z') ||
				('A' <= ch && ch <= 'Z') ||
				('0' <= ch && ch <= '9') ||
				(-1 != "._-".indexOf(ch)));
	}
}
