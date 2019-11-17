

package com.treegix.gateway;

import java.net.Socket;

import org.json.*;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

class SocketProcessor implements Runnable
{
	private static final Logger logger = LoggerFactory.getLogger(SocketProcessor.class);

	private Socket socket;

	SocketProcessor(Socket socket)
	{
		this.socket = socket;
	}

	@Override
	public void run()
	{
		logger.debug("starting to process incoming connection");

		BinaryProtocolSpeaker speaker = null;
		ItemChecker checker = null;

		try
		{
			speaker = new BinaryProtocolSpeaker(socket);

			JSONObject request = new JSONObject(speaker.getRequest());

			if (request.getString(ItemChecker.JSON_TAG_REQUEST).equals(ItemChecker.JSON_REQUEST_INTERNAL))
				checker = new InternalItemChecker(request);
			else if (request.getString(ItemChecker.JSON_TAG_REQUEST).equals(ItemChecker.JSON_REQUEST_JMX))
				checker = new JMXItemChecker(request);
			else
				throw new TreegixException("bad request tag value: '%s'", request.getString(ItemChecker.JSON_TAG_REQUEST));

			logger.debug("dispatched request to class {}", checker.getClass().getName());
			JSONArray values = checker.getValues();

			JSONObject response = new JSONObject();
			response.put(ItemChecker.JSON_TAG_RESPONSE, ItemChecker.JSON_RESPONSE_SUCCESS);
			response.put(ItemChecker.JSON_TAG_DATA, values);

			speaker.sendResponse(response.toString());
		}
		catch (Exception e1)
		{
			String error = TreegixException.getRootCauseMessage(e1);

			// Display first item key to identify items with incorrect configuration, all items in batch have same configuration.
			if (null == checker || null == checker.getFirstKey())
				logger.warn("error processing request: {}", error);
			else
				logger.warn("error processing request, item \"{}\" failed: {}", checker.getFirstKey(), error);

			logger.debug("error caused by", e1);

			try
			{
				JSONObject response = new JSONObject();
				response.put(ItemChecker.JSON_TAG_RESPONSE, ItemChecker.JSON_RESPONSE_FAILED);
				response.put(ItemChecker.JSON_TAG_ERROR, error);

				speaker.sendResponse(response.toString());
			}
			catch (Exception e2)
			{
				logger.warn("error sending failure notification: {}", TreegixException.getRootCauseMessage(e1));
				logger.debug("error caused by", e2);
			}
		}
		finally
		{
			try { if (null != speaker) speaker.close(); } catch (Exception e) { }
			try { if (null != socket) socket.close(); } catch (Exception e) { }
		}

		logger.debug("finished processing incoming connection");
	}
}
