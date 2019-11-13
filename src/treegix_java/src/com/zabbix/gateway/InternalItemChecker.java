/*
** Treegix
** Copyright (C) 2001-2019 Treegix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

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
