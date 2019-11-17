

#include <stdio.h>
#include <stdlib.h>
#include <winsock.h>

#include "treegix_sender.h"

/*
 * This is a simple Treegix sender utility implemented with
 * Treegix sender dynamic link library to illustrate the
 * library usage.
 *
 * See treegix_sender.h header file for API specifications.
 *
 * This utility can be built in Microsoft Windows 32 bit build
 * environment with the following command: nmake /f Makefile
 *
 * To run this utility ensure that treegix_sender.dll is
 * available (either in current directory or in windows/system
 * directories or in a directory defined in PATH variable)
 */

int main(int argc, char *argv[])
{
	if (5 == argc)
	{
		char			*result = NULL;
		treegix_sender_info_t	info;
		treegix_sender_value_t	value = {argv[2], argv[3], argv[4]};
		int			response;
		WSADATA			sockInfo;

		if (0 != WSAStartup(MAKEWORD(2, 2), &sockInfo))
		{
			printf("Cannot initialize Winsock DLL\n");
			return EXIT_FAILURE;
		}

		/* send one value to the argv[1] IP address and the default trapper port 10051 */
		if (-1 == treegix_sender_send_values(argv[1], 10051, NULL, &value, 1, &result))
		{
			printf("sending failed: %s\n", result);
		}
		else
		{
			printf("sending succeeded:\n");

			/* parse the server response */
			if (0 == treegix_sender_parse_result(result, &response, &info))
			{
				printf("  response: %s\n", 0 == response ? "success" : "failed");
				printf("  info from server: \"processed: %d; failed: %d; total: %d; seconds spent: %lf\"\n",
						info.total - info.failed, info.failed, info.total, info.time_spent);
			}
			else
				printf("  failed to parse server response\n");
		}

		/* free the server response */
		treegix_sender_free_result(result);
		while (0 == WSACleanup());
	}
	else
	{
		printf("Simple treegix_sender implementation with treegix_sender library\n\n");
		printf("usage: %s <server> <hostname> <key> <value>\n\n", argv[0]);
		printf("Options:\n");
		printf("  <server>    Hostname or IP address of Treegix server\n");
		printf("  <hostname>  Host name\n");
		printf("  <key>       Item key\n");
		printf("  <value>     Item value\n");
	}

	return EXIT_SUCCESS;
}
