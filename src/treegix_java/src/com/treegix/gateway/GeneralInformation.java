

package com.treegix.gateway;

class GeneralInformation
{
	static final String APPLICATION_NAME = "Treegix Java Gateway";
	static final String REVISION_DATE = "28 October 2019";
	static final String REVISION = "8870606e6a";
	static final String VERSION = "4.4.1";

	static void printVersion()
	{
		System.out.println(String.format("%s v%s (revision %s) (%s)", APPLICATION_NAME, VERSION, REVISION, REVISION_DATE));
	}
}
