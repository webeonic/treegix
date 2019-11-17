

package com.treegix.gateway;

class HelperFunctionChest
{
	static <T> boolean arrayContains(T[] array, T key)
	{
		for (T element : array)
		{
			if (key.equals(element))
				return true;
		}

		return false;
	}

	static int separatorIndex(String input)
	{
		for (int i = 0; i < input.length(); i++)
		{
			if ('\\' == input.charAt(i))
			{
				if (i + 1 < input.length() && ('\\' == input.charAt(i + 1) || '.' == input.charAt(i + 1)))
					i++;
			}
			else if ('.' == input.charAt(i))
			{
				return i;
			}
		}

		return -1;
	}

	static String unescapeUserInput(String input)
	{
		StringBuilder builder = new StringBuilder(input.length());

		for (int i = 0; i < input.length(); i++)
		{
			if ('\\' == input.charAt(i) && i + 1 < input.length() &&
					('\\' == input.charAt(i + 1) || '.' == input.charAt(i + 1)))
			{
				i++;
			}

			builder.append(input.charAt(i));
		}

		return builder.toString();
	}
}
