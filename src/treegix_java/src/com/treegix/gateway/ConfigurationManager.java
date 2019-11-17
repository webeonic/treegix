

package com.treegix.gateway;

import java.io.File;
import java.io.IOException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

class ConfigurationManager
{
	private static final Logger logger = LoggerFactory.getLogger(ConfigurationManager.class);

	static final String PID_FILE = "pidFile"; // has to be parsed first so that we remove the file if other parameters are bad
	static final String LISTEN_IP = "listenIP";
	static final String LISTEN_PORT = "listenPort";
	static final String START_POLLERS = "startPollers";
	static final String TIMEOUT = "timeout";

	private static ConfigurationParameter[] parameters =
	{
		new ConfigurationParameter(PID_FILE, ConfigurationParameter.TYPE_FILE, null,
				null,
				new PostInputValidator()
				{
					@Override
					public void execute(Object value)
					{
						logger.debug("received {} configuration parameter, daemonizing", PID_FILE);

						File pidFile = (File)value;

						pidFile.deleteOnExit();

						try
						{
							System.in.close();
							System.out.close();
							System.err.close();
						}
						catch (IOException e)
						{
							throw new RuntimeException(e);
						}
					}
				}),
		new ConfigurationParameter(LISTEN_IP, ConfigurationParameter.TYPE_INETADDRESS, null,
				null,
				null),
		new ConfigurationParameter(LISTEN_PORT, ConfigurationParameter.TYPE_INTEGER, 10052,
				new IntegerValidator(1024, 32767),
				null),
		new ConfigurationParameter(START_POLLERS, ConfigurationParameter.TYPE_INTEGER, 5,
				new IntegerValidator(1, 1000),
				null),
		new ConfigurationParameter(TIMEOUT, ConfigurationParameter.TYPE_INTEGER, 3,
				new IntegerValidator(1, 30),
				null)
	};

	static void parseConfiguration()
	{
		logger.debug("starting to parse configuration parameters");

		for (ConfigurationParameter parameter : parameters)
		{
			String property = System.getProperty("treegix." + parameter.getName());

			if (null != property)
			{
				logger.debug("found {} configuration parameter with value '{}'", parameter.getName(), property);
				parameter.setValue(property);
			}
		}

		logger.debug("finished parsing configuration parameters");
	}

	static ConfigurationParameter getParameter(String name)
	{
		for (ConfigurationParameter parameter : parameters)
			if (parameter.getName().equals(name))
				return parameter;

		throw new IllegalArgumentException("unknown configuration parameter: '" + name + "'");
	}

	static int getIntegerParameterValue(String name)
	{
		return (Integer)getParameter(name).getValue();
	}

	static String getPackage()
	{
		return "com.treegix.gateway";
	}
}
