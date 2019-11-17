

#ifndef TREEGIX_LIST_H
#define TREEGIX_LIST_H

#include "common.h"

/* list item data */
typedef struct list_item
{
	struct list_item	*next;
	void			*data;
}
zbx_list_item_t;

/* list data */
typedef struct
{
	zbx_list_item_t		*head;
	zbx_list_item_t		*tail;
}
zbx_list_t;

/* queue item data */
typedef struct
{
	zbx_list_t		*list;
	zbx_list_item_t		*current;
	zbx_list_item_t		*next;
}
zbx_list_iterator_t;

void	zbx_list_create(zbx_list_t *list);
void	zbx_list_destroy(zbx_list_t *list);
void	zbx_list_append(zbx_list_t *list, void *value, zbx_list_item_t **enqueued);
void	zbx_list_insert_after(zbx_list_t *list, zbx_list_item_t *after, void *value, zbx_list_item_t **enqueued);
void	zbx_list_prepend(zbx_list_t *list, void *value, zbx_list_item_t **enqueued);
int	zbx_list_pop(zbx_list_t *list, void **value);
int	zbx_list_peek(const zbx_list_t *list, void **value);
void	zbx_list_iterator_init(zbx_list_t *list, zbx_list_iterator_t *iterator);
int	zbx_list_iterator_next(zbx_list_iterator_t *iterator);
int	zbx_list_iterator_peek(const zbx_list_iterator_t *iterator, void **value);
void	zbx_list_iterator_clear(zbx_list_iterator_t *iterator);
int	zbx_list_iterator_equal(const zbx_list_iterator_t *iterator1, const zbx_list_iterator_t *iterator2);
int	zbx_list_iterator_isset(const zbx_list_iterator_t *iterator);
void	zbx_list_iterator_update(zbx_list_iterator_t *iterator);

#endif
