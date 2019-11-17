

package com.treegix.gateway;

import org.json.*;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

class InternalItemChecker extends ItemChecker
{
	private static final Logger logger = LoggerFactory.getLogger(InternalItemChecker.class);

	InternalItemChecker(JSONObject request) throws TreegixException
	{
		super(request);
	}

	@Override
	protected String getStringValue(String key) throws Exception
	{
		TreegixItem item = new TreegixItem(key);

		if (item.getKeyId().equals("treegix"))
		{
			if (3 != item.getArgumentCount() ||
					!item.getArgument(1).equals("java") ||
					!item.getArgument(2).equals(""))
				throw new TreegixException("required key format: treegix[java,,<parameter>]");

			String parameter = item.getArgument(3);

			if (parameter.equals("version"))
				return GeneralInformation.VERSION;
			else if (parameter.equals("ping"))
				return "1";
			else
				throw new TreegixException("third parameter '%s' is not supported", parameter);
		}
		else
			throw new TreegixException("key ID '%s' is not supported", item.getKeyId());
	}
}
