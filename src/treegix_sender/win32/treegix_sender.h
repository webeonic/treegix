

#ifndef TREEGIX_SENDER_H
#define TREEGIX_SENDER_H

#ifdef ZBX_EXPORT
#	define ZBX_API __declspec(dllexport)
#else
#	define ZBX_API __declspec(dllimport)
#endif

typedef struct
{
	/* host name, must match the name of target host in Treegix */
	char	*host;
	/* the item key */
	char	*key;
	/* the item value */
	char	*value;
}
treegix_sender_value_t;

typedef struct
{
	/* number of total values processed */
	int	total;
	/* number of failed values */
	int	failed;
	/* time in seconds the server spent processing the sent values */
	double	time_spent;
}
treegix_sender_info_t;

/******************************************************************************
 *                                                                            *
 * Function: treegix_sender_send_values                                        *
 *                                                                            *
 * Purpose: send values to Treegix server/proxy                                *
 *                                                                            *
 * Parameters: address   - [IN] treegix server/proxy address                   *
 *             port      - [IN] treegix server/proxy trapper port              *
 *             source    - [IN] source IP, optional - can be NULL             *
 *             values    - [IN] array of values to send                       *
 *             count     - [IN] number of items in values array               *
 *             result    - [OUT] the server response/error message, optional  *
 *                         If result is specified it must always be freed     *
 *                         afterwards with treegix_sender_free_result()        *
 *                         function.                                          *
 *                                                                            *
 * Return value: 0 - the values were sent successfully, result contains       *
 *                         server response                                    *
 *               -1 - an error occurred, result contains error message        *
 *                                                                            *
 ******************************************************************************/
ZBX_API int	treegix_sender_send_values(const char *address, unsigned short port, const char *source,
		const treegix_sender_value_t *values, int count, char **result);

/******************************************************************************
 *                                                                            *
 * Function: treegix_sender_parse_result                                       *
 *                                                                            *
 * Purpose: parses the result returned from treegix_sender_send_values()       *
 *          function                                                          *
 *                                                                            *
 * Parameters: result   - [IN] result to parse                                *
 *             response - [OUT] the operation response                        *
 *                           0 - operation was successful                     *
 *                          -1 - operation failed                             *
 *             info     - [OUT] the detailed information about processed      *
 *                        values, optional                                    *
 *                                                                            *
 * Return value:  0 - the result was parsed successfully                      *
 *               -1 - the result parsing failed                               *
 *                                                                            *
 * Comments: If info parameter was specified but the function failed to parse *
 *           the result info field, then info->total is set to -1.            *
 *                                                                            *
 ******************************************************************************/
ZBX_API int	treegix_sender_parse_result(const char *result, int *response, treegix_sender_info_t *info);

/******************************************************************************
 *                                                                            *
 * Function: treegix_sender_free_result                                        *
 *                                                                            *
 * Purpose: free data allocated by treegix_sender_send_values() function       *
 *                                                                            *
 * Parameters: ptr   - [IN] pointer to the data to free                       *
 *                                                                            *
 ******************************************************************************/
ZBX_API void	treegix_sender_free_result(void *ptr);

#endif	/* TREEGIX_SENDER_H */
