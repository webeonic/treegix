

#include "common.h"
#include "log.h"

#include "trxalgo.h"

static void	__hashset_free_entry(trx_hashset_t *hs, TRX_HASHSET_ENTRY_T *entry);

#define	CRIT_LOAD_FACTOR	4/5
#define	SLOT_GROWTH_FACTOR	3/2

#define TRX_HASHSET_DEFAULT_SLOTS	10

/* private hashset functions */

static void	__hashset_free_entry(trx_hashset_t *hs, TRX_HASHSET_ENTRY_T *entry)
{
	if (NULL != hs->clean_func)
		hs->clean_func(entry->data);

	hs->mem_free_func(entry);
}

static int	trx_hashset_init_slots(trx_hashset_t *hs, size_t init_size)
{
	hs->num_data = 0;

	if (0 < init_size)
	{
		hs->num_slots = next_prime(init_size);

		if (NULL == (hs->slots = (TRX_HASHSET_ENTRY_T **)hs->mem_malloc_func(NULL, hs->num_slots * sizeof(TRX_HASHSET_ENTRY_T *))))
			return FAIL;

		memset(hs->slots, 0, hs->num_slots * sizeof(TRX_HASHSET_ENTRY_T *));
	}
	else
	{
		hs->num_slots = 0;
		hs->slots = NULL;
	}

	return SUCCEED;
}

/* public hashset interface */

void	trx_hashset_create(trx_hashset_t *hs, size_t init_size,
				trx_hash_func_t hash_func,
				trx_compare_func_t compare_func)
{
	trx_hashset_create_ext(hs, init_size, hash_func, compare_func, NULL,
					TRX_DEFAULT_MEM_MALLOC_FUNC,
					TRX_DEFAULT_MEM_REALLOC_FUNC,
					TRX_DEFAULT_MEM_FREE_FUNC);
}

void	trx_hashset_create_ext(trx_hashset_t *hs, size_t init_size,
				trx_hash_func_t hash_func,
				trx_compare_func_t compare_func,
				trx_clean_func_t clean_func,
				trx_mem_malloc_func_t mem_malloc_func,
				trx_mem_realloc_func_t mem_realloc_func,
				trx_mem_free_func_t mem_free_func)
{
	hs->hash_func = hash_func;
	hs->compare_func = compare_func;
	hs->clean_func = clean_func;
	hs->mem_malloc_func = mem_malloc_func;
	hs->mem_realloc_func = mem_realloc_func;
	hs->mem_free_func = mem_free_func;

	trx_hashset_init_slots(hs, init_size);
}

void	trx_hashset_destroy(trx_hashset_t *hs)
{
	int			i;
	TRX_HASHSET_ENTRY_T	*entry, *next_entry;

	for (i = 0; i < hs->num_slots; i++)
	{
		entry = hs->slots[i];

		while (NULL != entry)
		{
			next_entry = entry->next;
			__hashset_free_entry(hs, entry);
			entry = next_entry;
		}
	}

	hs->num_data = 0;
	hs->num_slots = 0;

	if (NULL != hs->slots)
	{
		hs->mem_free_func(hs->slots);
		hs->slots = NULL;
	}

	hs->hash_func = NULL;
	hs->compare_func = NULL;
	hs->mem_malloc_func = NULL;
	hs->mem_realloc_func = NULL;
	hs->mem_free_func = NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_hashset_reserve                                              *
 *                                                                            *
 * Purpose: allocation not less than the required number of slots for hashset *
 *                                                                            *
 * Parameters: hs            - [IN] the destination hashset                   *
 *             num_slots_req - [IN] the number of required slots              *
 *                                                                            *
 ******************************************************************************/
int	trx_hashset_reserve(trx_hashset_t *hs, int num_slots_req)
{
	if (0 == hs->num_slots)
	{
		/* correction for prevent the second relocation in the case that requires the same number of slots */
		if (SUCCEED != trx_hashset_init_slots(hs, MAX(TRX_HASHSET_DEFAULT_SLOTS,
				num_slots_req * (2 - CRIT_LOAD_FACTOR) + 1)))
		{
			return FAIL;
		}
	}
	else if (num_slots_req >= hs->num_slots * CRIT_LOAD_FACTOR)
	{
		int			inc_slots, new_slot, slot;
		void			*slots;
		TRX_HASHSET_ENTRY_T	**prev_next, *curr_entry, *tmp;

		inc_slots = next_prime(hs->num_slots * SLOT_GROWTH_FACTOR);

		if (NULL == (slots = hs->mem_realloc_func(hs->slots, inc_slots * sizeof(TRX_HASHSET_ENTRY_T *))))
			return FAIL;

		hs->slots = (TRX_HASHSET_ENTRY_T **)slots;

		memset(hs->slots + hs->num_slots, 0, (inc_slots - hs->num_slots) * sizeof(TRX_HASHSET_ENTRY_T *));

		for (slot = 0; slot < hs->num_slots; slot++)
		{
			prev_next = &hs->slots[slot];
			curr_entry = hs->slots[slot];

			while (NULL != curr_entry)
			{
				if (slot != (new_slot = curr_entry->hash % inc_slots))
				{
					tmp = curr_entry->next;
					curr_entry->next = hs->slots[new_slot];
					hs->slots[new_slot] = curr_entry;

					*prev_next = tmp;
					curr_entry = tmp;
				}
				else
				{
					prev_next = &curr_entry->next;
					curr_entry = curr_entry->next;
				}
			}
		}

		hs->num_slots = inc_slots;
	}

	return SUCCEED;
}

void	*trx_hashset_insert(trx_hashset_t *hs, const void *data, size_t size)
{
	return trx_hashset_insert_ext(hs, data, size, 0);
}

void	*trx_hashset_insert_ext(trx_hashset_t *hs, const void *data, size_t size, size_t offset)
{
	int			slot;
	trx_hash_t		hash;
	TRX_HASHSET_ENTRY_T	*entry;

	if (0 == hs->num_slots && SUCCEED != trx_hashset_init_slots(hs, TRX_HASHSET_DEFAULT_SLOTS))
		return NULL;

	hash = hs->hash_func(data);

	slot = hash % hs->num_slots;
	entry = hs->slots[slot];

	while (NULL != entry)
	{
		if (entry->hash == hash && hs->compare_func(entry->data, data) == 0)
			break;

		entry = entry->next;
	}

	if (NULL == entry)
	{
		if (SUCCEED != trx_hashset_reserve(hs, hs->num_data + 1))
			return NULL;

		/* recalculate new slot */
		slot = hash % hs->num_slots;

		if (NULL == (entry = (TRX_HASHSET_ENTRY_T *)hs->mem_malloc_func(NULL, offsetof(TRX_HASHSET_ENTRY_T, data) + size)))
			return NULL;

		memcpy((char *)entry->data + offset, (const char *)data + offset, size - offset);
		entry->hash = hash;
		entry->next = hs->slots[slot];
		hs->slots[slot] = entry;
		hs->num_data++;
	}

	return entry->data;
}

void	*trx_hashset_search(trx_hashset_t *hs, const void *data)
{
	int			slot;
	trx_hash_t		hash;
	TRX_HASHSET_ENTRY_T	*entry;

	if (0 == hs->num_slots)
		return NULL;

	hash = hs->hash_func(data);

	slot = hash % hs->num_slots;
	entry = hs->slots[slot];

	while (NULL != entry)
	{
		if (entry->hash == hash && hs->compare_func(entry->data, data) == 0)
			break;

		entry = entry->next;
	}

	return (NULL != entry ? entry->data : NULL);
}

/******************************************************************************
 *                                                                            *
 * Purpose: remove a hashset entry using comparison with the given data       *
 *                                                                            *
 ******************************************************************************/
void	trx_hashset_remove(trx_hashset_t *hs, const void *data)
{
	int			slot;
	trx_hash_t		hash;
	TRX_HASHSET_ENTRY_T	*entry;

	if (0 == hs->num_slots)
		return;

	hash = hs->hash_func(data);

	slot = hash % hs->num_slots;
	entry = hs->slots[slot];

	if (NULL != entry)
	{
		if (entry->hash == hash && hs->compare_func(entry->data, data) == 0)
		{
			hs->slots[slot] = entry->next;
			__hashset_free_entry(hs, entry);
			hs->num_data--;
		}
		else
		{
			TRX_HASHSET_ENTRY_T	*prev_entry;

			prev_entry = entry;
			entry = entry->next;

			while (NULL != entry)
			{
				if (entry->hash == hash && hs->compare_func(entry->data, data) == 0)
				{
					prev_entry->next = entry->next;
					__hashset_free_entry(hs, entry);
					hs->num_data--;
					break;
				}

				prev_entry = entry;
				entry = entry->next;
			}
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: remove a hashset entry using a data pointer returned to the user  *
 *          by trx_hashset_insert[_ext]() and trx_hashset_search() functions  *
 *                                                                            *
 ******************************************************************************/
void	trx_hashset_remove_direct(trx_hashset_t *hs, const void *data)
{
	int			slot;
	TRX_HASHSET_ENTRY_T	*data_entry, *iter_entry;

	if (0 == hs->num_slots)
		return;

	data_entry = (TRX_HASHSET_ENTRY_T *)((const char *)data - offsetof(TRX_HASHSET_ENTRY_T, data));

	slot = data_entry->hash % hs->num_slots;
	iter_entry = hs->slots[slot];

	if (NULL != iter_entry)
	{
		if (iter_entry == data_entry)
		{
			hs->slots[slot] = data_entry->next;
			__hashset_free_entry(hs, data_entry);
			hs->num_data--;
		}
		else
		{
			while (NULL != iter_entry->next)
			{
				if (iter_entry->next == data_entry)
				{
					iter_entry->next = data_entry->next;
					__hashset_free_entry(hs, data_entry);
					hs->num_data--;
					break;
				}

				iter_entry = iter_entry->next;
			}
		}
	}
}

void	trx_hashset_clear(trx_hashset_t *hs)
{
	int			slot;
	TRX_HASHSET_ENTRY_T	*entry;

	for (slot = 0; slot < hs->num_slots; slot++)
	{
		while (NULL != hs->slots[slot])
		{
			entry = hs->slots[slot];
			hs->slots[slot] = entry->next;
			__hashset_free_entry(hs, entry);
		}
	}

	hs->num_data = 0;
}

#define	ITER_START	(-1)
#define	ITER_FINISH	(-2)

void	trx_hashset_iter_reset(trx_hashset_t *hs, trx_hashset_iter_t *iter)
{
	iter->hashset = hs;
	iter->slot = ITER_START;
}

void	*trx_hashset_iter_next(trx_hashset_iter_t *iter)
{
	if (ITER_FINISH == iter->slot)
		return NULL;

	if (ITER_START != iter->slot && NULL != iter->entry && NULL != iter->entry->next)
	{
		iter->entry = iter->entry->next;
		return iter->entry->data;
	}

	while (1)
	{
		iter->slot++;

		if (iter->slot == iter->hashset->num_slots)
		{
			iter->slot = ITER_FINISH;
			return NULL;
		}

		if (NULL != iter->hashset->slots[iter->slot])
		{
			iter->entry = iter->hashset->slots[iter->slot];
			return iter->entry->data;
		}
	}
}

void	trx_hashset_iter_remove(trx_hashset_iter_t *iter)
{
	if (ITER_START == iter->slot || ITER_FINISH == iter->slot || NULL == iter->entry)
	{
		treegix_log(LOG_LEVEL_CRIT, "removing a hashset entry through a bad iterator");
		exit(EXIT_FAILURE);
	}

	if (iter->hashset->slots[iter->slot] == iter->entry)
	{
		iter->hashset->slots[iter->slot] = iter->entry->next;
		__hashset_free_entry(iter->hashset, iter->entry);
		iter->hashset->num_data--;

		iter->slot--;
		iter->entry = NULL;
	}
	else
	{
		TRX_HASHSET_ENTRY_T	*prev_entry = iter->hashset->slots[iter->slot];

		while (prev_entry->next != iter->entry)
			prev_entry = prev_entry->next;

		prev_entry->next = iter->entry->next;
		__hashset_free_entry(iter->hashset, iter->entry);
		iter->hashset->num_data--;

		iter->entry = prev_entry;
	}
}
