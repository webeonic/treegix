

#ifndef TREEGIX_LIST_H
#define TREEGIX_LIST_H

#include "common.h"

/* list item data */
typedef struct list_item
{
	struct list_item	*next;
	void			*data;
}
trx_list_item_t;

/* list data */
typedef struct
{
	trx_list_item_t		*head;
	trx_list_item_t		*tail;
}
trx_list_t;

/* queue item data */
typedef struct
{
	trx_list_t		*list;
	trx_list_item_t		*current;
	trx_list_item_t		*next;
}
trx_list_iterator_t;

void	trx_list_create(trx_list_t *list);
void	trx_list_destroy(trx_list_t *list);
void	trx_list_append(trx_list_t *list, void *value, trx_list_item_t **enqueued);
void	trx_list_insert_after(trx_list_t *list, trx_list_item_t *after, void *value, trx_list_item_t **enqueued);
void	trx_list_prepend(trx_list_t *list, void *value, trx_list_item_t **enqueued);
int	trx_list_pop(trx_list_t *list, void **value);
int	trx_list_peek(const trx_list_t *list, void **value);
void	trx_list_iterator_init(trx_list_t *list, trx_list_iterator_t *iterator);
int	trx_list_iterator_next(trx_list_iterator_t *iterator);
int	trx_list_iterator_peek(const trx_list_iterator_t *iterator, void **value);
void	trx_list_iterator_clear(trx_list_iterator_t *iterator);
int	trx_list_iterator_equal(const trx_list_iterator_t *iterator1, const trx_list_iterator_t *iterator2);
int	trx_list_iterator_isset(const trx_list_iterator_t *iterator);
void	trx_list_iterator_update(trx_list_iterator_t *iterator);

#endif
