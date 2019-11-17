

package com.treegix.gateway;

import java.util.ArrayList;

import org.json.*;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

abstract class ItemChecker
{
	private static final Logger logger = LoggerFactory.getLogger(ItemChecker.class);

	static final String JSON_TAG_DATA = "data";
	static final String JSON_TAG_ERROR = "error";
	static final String JSON_TAG_KEYS = "keys";
	static final String JSON_TAG_PASSWORD = "password";
	static final String JSON_TAG_REQUEST = "request";
	static final String JSON_TAG_RESPONSE = "response";
	static final String JSON_TAG_USERNAME = "username";
	static final String JSON_TAG_VALUE = "value";
	static final String JSON_TAG_JMX_ENDPOINT = "jmx_endpoint";

	static final String JSON_REQUEST_INTERNAL = "java gateway internal";
	static final String JSON_REQUEST_JMX = "java gateway jmx";

	static final String JSON_RESPONSE_FAILED = "failed";
	static final String JSON_RESPONSE_SUCCESS = "success";

	protected JSONObject request;
	protected ArrayList<String> keys;

	protected ItemChecker(JSONObject request) throws TreegixException
	{
		this.request = request;

		try
		{
			JSONArray jsonKeys = request.getJSONArray(JSON_TAG_KEYS);
			keys = new ArrayList<String>();

			for (int i = 0; i < jsonKeys.length(); i++)
				keys.add(jsonKeys.getString(i));
		}
		catch (Exception e)
		{
			throw new TreegixException(e);
		}
	}

	JSONArray getValues() throws TreegixException
	{
		JSONArray values = new JSONArray();

		for (String key : keys)
			values.put(getJSONValue(key));

		return values;
	}

	String getFirstKey()
	{
		return 0 == keys.size() ? null : keys.get(0);
	}

	protected final JSONObject getJSONValue(String key)
	{
		JSONObject value = new JSONObject();

		try
		{
			logger.debug("getting value for item '{}'", key);
			String text = getStringValue(key);
			logger.debug("received value '{}' for item '{}'", text, key);
			value.put(JSON_TAG_VALUE, text);
		}
		catch (Exception e1)
		{
			try
			{
				logger.debug("caught exception for item '{}'", key, e1);
				value.put(JSON_TAG_ERROR, TreegixException.getRootCauseMessage(e1));
			}
			catch (JSONException e2)
			{
				Object[] logInfo = {JSON_TAG_ERROR, e1.getMessage(), TreegixException.getRootCauseMessage(e2)};
				logger.warn("cannot add JSON attribute '{}' with message '{}': {}", logInfo);
				logger.debug("error caused by", e2);
			}
		}

		return value;
	}

	protected abstract String getStringValue(String key) throws Exception;
}
