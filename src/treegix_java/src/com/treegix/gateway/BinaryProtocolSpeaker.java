

package com.treegix.gateway;

import java.io.*;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.Charset;
import java.util.Arrays;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

class BinaryProtocolSpeaker
{
	private static final Logger logger = LoggerFactory.getLogger(BinaryProtocolSpeaker.class);

	private static final byte[] PROTOCOL_HEADER = {'Z', 'B', 'X', 'D', '\1'};
	private static final Charset UTF8_CHARSET = Charset.forName("UTF-8");

	private Socket socket;
	private DataInputStream dis = null;
	private BufferedOutputStream bos = null;

	BinaryProtocolSpeaker(Socket socket)
	{
		this.socket = socket;
	}

	String getRequest() throws IOException, TreegixException
	{
		dis = new DataInputStream(socket.getInputStream());

		byte[] data;

		logger.debug("reading Treegix protocol header");
		data = new byte[5];
		dis.readFully(data);

		if (!Arrays.equals(data, PROTOCOL_HEADER))
			throw new TreegixException("bad protocol header: %02X %02X %02X %02X %02X", data[0], data[1], data[2], data[3], data[4]);

		logger.debug("reading 8 bytes of data length");
		data = new byte[8];
		dis.readFully(data);

		ByteBuffer buffer = ByteBuffer.wrap(data);
		buffer.order(ByteOrder.LITTLE_ENDIAN);
		long length = buffer.getLong();

		if (!(0 <= length && length <= Integer.MAX_VALUE))
			throw new TreegixException("bad data length: %d", length);

		logger.debug("reading {} bytes of request data", length);
		data = new byte[(int)length];
		dis.readFully(data);

		String request = new String(data, UTF8_CHARSET);
		logger.debug("received the following data in request: {}", request);
		return request;
	}

	void sendResponse(String response) throws IOException, TreegixException
	{
		bos = new BufferedOutputStream(socket.getOutputStream());

		logger.debug("sending the following data in response: {}", response);

		byte[] data, responseBytes;

		data = PROTOCOL_HEADER;
		bos.write(data, 0, data.length);

		responseBytes = response.getBytes(UTF8_CHARSET);
		ByteBuffer buffer = ByteBuffer.allocate(8);
		buffer.order(ByteOrder.LITTLE_ENDIAN);
		buffer.putLong(responseBytes.length);
		data = buffer.array();
		bos.write(data, 0, data.length);

		data = responseBytes;
		bos.write(data, 0, data.length);

		bos.flush();
	}

	void close()
	{
		try { if (null != dis) dis.close(); } catch (Exception e) { }
		try { if (null != bos) bos.close(); } catch (Exception e) { }
		try { if (null != socket) socket.close(); } catch (Exception e) { }
	}
}
