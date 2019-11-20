

#include "common.h"
#include "linked_list.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_list_create                                                  *
 *                                                                            *
 * Purpose: create singly linked list                                         *
 *                                                                            *
 * Parameters: list - [IN] the list                                           *
 *                                                                            *
 ******************************************************************************/
void	trx_list_create(trx_list_t *queue)
{
	memset(queue, 0, sizeof(*queue));
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_destroy                                                 *
 *                                                                            *
 * Purpose: destroy list                                                      *
 *                                                                            *
 * Parameters: list - [IN] the list                                           *
 *                                                                            *
 ******************************************************************************/
void	trx_list_destroy(trx_list_t *list)
{
	while (FAIL != trx_list_pop(list, NULL))
		;
}

/******************************************************************************
 *                                                                            *
 * Function: list_create_item                                                 *
 *                                                                            *
 * Purpose: allocate memory and initialize a new list item                    *
 *                                                                            *
 * Parameters: list     - [IN] the list                                       *
 *             value    - [IN] the data to be stored                          *
 *             created  - [OUT] pointer to the created list item              *
 *                                                                            *
 ******************************************************************************/
static void	list_create_item(trx_list_t *list, void *value, trx_list_item_t **created)
{
	trx_list_item_t *item;

	TRX_UNUSED(list);

	item = (trx_list_item_t *)trx_malloc(NULL, sizeof(trx_list_item_t));
	item->next = NULL;
	item->data = value;

	*created = item;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_insert_after                                            *
 *                                                                            *
 * Purpose: insert value after specified position in the list                 *
 *                                                                            *
 * Parameters: list     - [IN] the list                                       *
 *             after    - [IN] specified position (can be NULL to insert at   *
 *                             the end of the list)                           *
 *             value    - [IN] the value to be inserted                       *
 *             inserted - [OUT] pointer to the inserted list item             *
 *                                                                            *
 ******************************************************************************/
void	trx_list_insert_after(trx_list_t *list, trx_list_item_t *after, void *value, trx_list_item_t **inserted)
{
	trx_list_item_t *item;

	list_create_item(list, value, &item);

	if (NULL == after)
		after = list->tail;

	if (NULL != after)
	{
		item->next = after->next;
		after->next = item;
	}
	else
		list->head = item;

	if (after == list->tail)
		list->tail = item;

	if (NULL != inserted)
		*inserted = item;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_append                                                  *
 *                                                                            *
 * Purpose: append value to the end of the list                               *
 *                                                                            *
 * Parameters: list     - [IN] the list                                       *
 *             value    - [IN] the value to append                            *
 *             inserted - [OUT] pointer to the inserted list item             *
 *                                                                            *
 ******************************************************************************/
void	trx_list_append(trx_list_t *list, void *value, trx_list_item_t **inserted)
{
	trx_list_insert_after(list, NULL, value, inserted);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_prepend                                                 *
 *                                                                            *
 * Purpose: prepend value to the beginning of the list                        *
 *                                                                            *
 * Parameters: list     - [IN] the list                                       *
 *             value    - [IN] the value to prepend                           *
 *             inserted - [OUT] pointer to the inserted list item             *
 *                                                                            *
 ******************************************************************************/
void	trx_list_prepend(trx_list_t *list, void *value, trx_list_item_t **inserted)
{
	trx_list_item_t *item;

	list_create_item(list, value, &item);
	item->next = list->head;
	list->head = item;

	if (NULL == list->tail)
		list->tail = item;

	if (NULL != inserted)
		*inserted = item;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_pop                                                     *
 *                                                                            *
 * Purpose: removes a value from the beginning of the list                    *
 *                                                                            *
 * Parameters: list  - [IN]  the list                                         *
 *             value - [OUT] the value                                        *
 *                                                                            *
 * Return value: SUCCEED is returned if list is not empty, otherwise, FAIL is *
 *               returned.                                                    *
 *                                                                            *
 ******************************************************************************/
int	trx_list_pop(trx_list_t *list, void **value)
{
	trx_list_item_t	*head;

	if (NULL == list->head)
		return FAIL;

	head = list->head;

	if (NULL != value)
		*value = head->data;

	list->head = list->head->next;
	trx_free(head);

	if (NULL == list->head)
		list->tail = NULL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_peek                                                    *
 *                                                                            *
 * Purpose: get value from the queue without dequeuing                        *
 *                                                                            *
 * Parameters: list  - [IN]  the list                                         *
 *             value - [OUT] the value                                        *
 *                                                                            *
 * Return value: SUCCEED is returned if list is not empty, otherwise, FAIL is *
 *               returned.                                                    *
 *                                                                            *
 ******************************************************************************/
int	trx_list_peek(const trx_list_t *list, void **value)
{
	if (NULL != list->head)
	{
		*value = list->head->data;
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_iterator_init                                           *
 *                                                                            *
 * Purpose: initialize list iterator                                          *
 *                                                                            *
 * Parameters: list     - [IN]  the list                                      *
 *             iterator - [OUT] the iterator to be initialized                *
 *                                                                            *
 ******************************************************************************/
void	trx_list_iterator_init(trx_list_t *list, trx_list_iterator_t *iterator)
{
	iterator->list = list;
	iterator->next = list->head;
	iterator->current = NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_iterator_next                                           *
 *                                                                            *
 * Purpose: advance list iterator                                             *
 *                                                                            *
 * Parameters: iterator - [IN] the iterator to be advanced                    *
 *                                                                            *
 * Return value: SUCCEED is returned if next list item exists, otherwise,     *
 *               FAIL is returned.                                            *
 *                                                                            *
 ******************************************************************************/
int	trx_list_iterator_next(trx_list_iterator_t *iterator)
{
	if (NULL != iterator->next)
	{
		iterator->current = iterator->next;
		iterator->next = iterator->next->next;

		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_iterator_peek                                           *
 *                                                                            *
 * Purpose: get value without removing it from list                           *
 *                                                                            *
 * Parameters: iterator - [IN]  initialized list iterator                     *
 *             value    - [OUT] the value                                     *
 *                                                                            *
 * Return value: SUCCEED is returned if item exists, otherwise, FAIL is       *
 *               returned.                                                    *
 *                                                                            *
 ******************************************************************************/
int	trx_list_iterator_peek(const trx_list_iterator_t *iterator, void **value)
{
	if (NULL != iterator->current)
	{
		*value = iterator->current->data;
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_iterator_clear                                          *
 *                                                                            *
 * Purpose: clears iterator leaving it in uninitialized state                 *
 *                                                                            *
 * Parameters: iterator - [IN]  list iterator                                 *
 *                                                                            *
 ******************************************************************************/
void	trx_list_iterator_clear(trx_list_iterator_t *iterator)
{
	memset(iterator, 0, sizeof(trx_list_iterator_t));
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_iterator_equal                                          *
 *                                                                            *
 * Purpose: tests if two iterators points at the same list item               *
 *                                                                            *
 * Parameters: iterator1 - [IN] first list iterator                           *
 *             iterator2 - [IN] second list iterator                          *
 *                                                                            *
 * Return value: SUCCEED is returned if both iterator point at the same item, *
 *               FAIL otherwise.                                              *
 *                                                                            *
 ******************************************************************************/
int	trx_list_iterator_equal(const trx_list_iterator_t *iterator1, const trx_list_iterator_t *iterator2)
{
	if (iterator1->list == iterator2->list && iterator1->current == iterator2->current)
		return SUCCEED;

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_iterator_isset                                          *
 *                                                                            *
 * Purpose: checks if the iterator points at some list item                   *
 *                                                                            *
 * Parameters: iterator - [IN] list iterator                                  *
 *                                                                            *
 * Return value: SUCCEED is returned if iterator is set, FAIL otherwise.      *
 *                                                                            *
 ******************************************************************************/
int	trx_list_iterator_isset(const trx_list_iterator_t *iterator)
{
	return (NULL == iterator->list ? FAIL : SUCCEED);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_list_iterator_update                                         *
 *                                                                            *
 * Purpose: updates iterator                                                  *
 *                                                                            *
 * Parameters: iterator - [IN] list iterator                                  *
 *                                                                            *
 * Comments: This function must be used after an item has been inserted in    *
 *           list during iteration process.                                   *
 *                                                                            *
 ******************************************************************************/
void	trx_list_iterator_update(trx_list_iterator_t *iterator)
{
	if (NULL != iterator->current)
		iterator->next = iterator->current->next;
}
