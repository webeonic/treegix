

package com.treegix.gateway;

import java.net.InetAddress;
import java.net.ServerSocket;
import java.util.concurrent.*;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class JavaGateway
{
	private static final Logger logger = LoggerFactory.getLogger(JavaGateway.class);

	public static void main(String[] args)
	{
		if (1 == args.length && (args[0].equals("-V") || args[0].equals("--version")))
		{
			GeneralInformation.printVersion();
			System.exit(0);
		}
		else if (0 != args.length)
		{
			System.out.println("unsupported command line options");
			System.exit(1);
		}

		logger.info("Treegix Java Gateway {} (revision {}) has started", GeneralInformation.VERSION, GeneralInformation.REVISION);

		Thread shutdownHook = new Thread()
		{
			@Override
			public void run()
			{
				logger.info("Treegix Java Gateway {} (revision {}) has stopped", GeneralInformation.VERSION, GeneralInformation.REVISION);
			}
		};

		Runtime.getRuntime().addShutdownHook(shutdownHook);

		try
		{
			ConfigurationManager.parseConfiguration();

			InetAddress listenIP = (InetAddress)ConfigurationManager.getParameter(ConfigurationManager.LISTEN_IP).getValue();
			int listenPort = ConfigurationManager.getIntegerParameterValue(ConfigurationManager.LISTEN_PORT);

			ServerSocket socket = new ServerSocket(listenPort, 0, listenIP);
			socket.setReuseAddress(true);
			logger.info("listening on {}:{}", socket.getInetAddress(), socket.getLocalPort());

			int startPollers = ConfigurationManager.getIntegerParameterValue(ConfigurationManager.START_POLLERS);
			ExecutorService threadPool = new ThreadPoolExecutor(
					startPollers,
					startPollers,
					30L, TimeUnit.SECONDS,
					new ArrayBlockingQueue<Runnable>(startPollers),
					new ThreadPoolExecutor.CallerRunsPolicy());
			logger.debug("created a thread pool of {} pollers", startPollers);

			while (true)
				threadPool.execute(new SocketProcessor(socket.accept()));
		}
		catch (Exception e)
		{
			logger.error("caught fatal exception: {}", TreegixException.getRootCauseMessage(e));
			logger.debug("error caused by", e);
		}
	}
}
