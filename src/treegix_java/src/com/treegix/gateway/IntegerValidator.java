

package com.treegix.gateway;

class IntegerValidator implements InputValidator
{
	private int lo;
	private int hi;

	IntegerValidator(int lo, int hi)
	{
		if (lo > hi)
			throw new IllegalArgumentException("bad validation bounds: " + lo + " and " + hi);

		this.lo = lo;
		this.hi = hi;
	}

	@Override
	public boolean validate(Object value)
	{
		if (value instanceof Integer)
		{
			Integer integer = (Integer)value;

			if (!(Integer.valueOf(lo).compareTo(integer) <= 0))
				return false;

			if (!(integer.compareTo(Integer.valueOf(hi)) <= 0))
				return false;

			return true;
		}
		else
			return false;
	}
}
