

#include "common.h"
#include "log.h"

#include "trxalgo.h"

static void	__hashmap_ensure_free_entry(trx_hashmap_t *hm, TRX_HASHMAP_SLOT_T *slot);

#define	CRIT_LOAD_FACTOR	5/1
#define	SLOT_GROWTH_FACTOR	3/2

#define ARRAY_GROWTH_FACTOR	2 /* because the number of slot entries is usually small, with 3/2 they grow too slow */

#define TRX_HASHMAP_DEFAULT_SLOTS	10

/* private hashmap functions */

static void	__hashmap_ensure_free_entry(trx_hashmap_t *hm, TRX_HASHMAP_SLOT_T *slot)
{
	if (NULL == slot->entries)
	{
		slot->entries_num = 0;
		slot->entries_alloc = 6;
		slot->entries = (TRX_HASHMAP_ENTRY_T *)hm->mem_malloc_func(NULL, slot->entries_alloc * sizeof(TRX_HASHMAP_ENTRY_T));
	}
	else if (slot->entries_num == slot->entries_alloc)
	{
		slot->entries_alloc = slot->entries_alloc * ARRAY_GROWTH_FACTOR;
		slot->entries = (TRX_HASHMAP_ENTRY_T *)hm->mem_realloc_func(slot->entries, slot->entries_alloc * sizeof(TRX_HASHMAP_ENTRY_T));
	}
}

static void	trx_hashmap_init_slots(trx_hashmap_t *hm, size_t init_size)
{
	hm->num_data = 0;

	if (0 < init_size)
	{
		hm->num_slots = next_prime(init_size);
		hm->slots = (TRX_HASHMAP_SLOT_T *)hm->mem_malloc_func(NULL, hm->num_slots * sizeof(TRX_HASHMAP_SLOT_T));
		memset(hm->slots, 0, hm->num_slots * sizeof(TRX_HASHMAP_SLOT_T));
	}
	else
	{
		hm->num_slots = 0;
		hm->slots = NULL;
	}
}

/* public hashmap interface */

void	trx_hashmap_create(trx_hashmap_t *hm, size_t init_size)
{
	trx_hashmap_create_ext(hm, init_size,
				TRX_DEFAULT_UINT64_HASH_FUNC,
				TRX_DEFAULT_UINT64_COMPARE_FUNC,
				TRX_DEFAULT_MEM_MALLOC_FUNC,
				TRX_DEFAULT_MEM_REALLOC_FUNC,
				TRX_DEFAULT_MEM_FREE_FUNC);
}

void	trx_hashmap_create_ext(trx_hashmap_t *hm, size_t init_size,
				trx_hash_func_t hash_func,
				trx_compare_func_t compare_func,
				trx_mem_malloc_func_t mem_malloc_func,
				trx_mem_realloc_func_t mem_realloc_func,
				trx_mem_free_func_t mem_free_func)
{
	hm->hash_func = hash_func;
	hm->compare_func = compare_func;
	hm->mem_malloc_func = mem_malloc_func;
	hm->mem_realloc_func = mem_realloc_func;
	hm->mem_free_func = mem_free_func;

	trx_hashmap_init_slots(hm, init_size);
}

void	trx_hashmap_destroy(trx_hashmap_t *hm)
{
	int	i;

	for (i = 0; i < hm->num_slots; i++)
	{
		if (NULL != hm->slots[i].entries)
			hm->mem_free_func(hm->slots[i].entries);
	}

	hm->num_data = 0;
	hm->num_slots = 0;

	if (NULL != hm->slots)
	{
		hm->mem_free_func(hm->slots);
		hm->slots = NULL;
	}

	hm->hash_func = NULL;
	hm->compare_func = NULL;
	hm->mem_malloc_func = NULL;
	hm->mem_realloc_func = NULL;
	hm->mem_free_func = NULL;
}

int	trx_hashmap_get(trx_hashmap_t *hm, trx_uint64_t key)
{
	int			i, value = FAIL;
	trx_hash_t		hash;
	TRX_HASHMAP_SLOT_T	*slot;

	if (0 == hm->num_slots)
		return FAIL;

	hash = hm->hash_func(&key);
	slot = &hm->slots[hash % hm->num_slots];

	for (i = 0; i < slot->entries_num; i++)
	{
		if (0 == hm->compare_func(&slot->entries[i].key, &key))
		{
			value = slot->entries[i].value;
			break;
		}
	}

	return value;
}

void	trx_hashmap_set(trx_hashmap_t *hm, trx_uint64_t key, int value)
{
	int			i;
	trx_hash_t		hash;
	TRX_HASHMAP_SLOT_T	*slot;

	if (0 == hm->num_slots)
		trx_hashmap_init_slots(hm, TRX_HASHMAP_DEFAULT_SLOTS);

	hash = hm->hash_func(&key);
	slot = &hm->slots[hash % hm->num_slots];

	for (i = 0; i < slot->entries_num; i++)
	{
		if (0 == hm->compare_func(&slot->entries[i].key, &key))
		{
			slot->entries[i].value = value;
			break;
		}
	}

	if (i == slot->entries_num)
	{
		__hashmap_ensure_free_entry(hm, slot);
		slot->entries[i].key = key;
		slot->entries[i].value = value;
		slot->entries_num++;
		hm->num_data++;

		if (hm->num_data >= hm->num_slots * CRIT_LOAD_FACTOR)
		{
			int			inc_slots, s;
			TRX_HASHMAP_SLOT_T	*new_slot;

			inc_slots = next_prime(hm->num_slots * SLOT_GROWTH_FACTOR);

			hm->slots = (TRX_HASHMAP_SLOT_T *)hm->mem_realloc_func(hm->slots, inc_slots * sizeof(TRX_HASHMAP_SLOT_T));
			memset(hm->slots + hm->num_slots, 0, (inc_slots - hm->num_slots) * sizeof(TRX_HASHMAP_SLOT_T));

			for (s = 0; s < hm->num_slots; s++)
			{
				slot = &hm->slots[s];

				for (i = 0; i < slot->entries_num; i++)
				{
					hash = hm->hash_func(&slot->entries[i].key);
					new_slot = &hm->slots[hash % inc_slots];

					if (slot != new_slot)
					{
						__hashmap_ensure_free_entry(hm, new_slot);
						new_slot->entries[new_slot->entries_num] = slot->entries[i];
						new_slot->entries_num++;

						slot->entries[i] = slot->entries[slot->entries_num - 1];
						slot->entries_num--;
						i--;
					}
				}
			}

			hm->num_slots = inc_slots;
		}
	}
}

void	trx_hashmap_remove(trx_hashmap_t *hm, trx_uint64_t key)
{
	int			i;
	trx_hash_t		hash;
	TRX_HASHMAP_SLOT_T	*slot;

	if (0 == hm->num_slots)
		return;

	hash = hm->hash_func(&key);
	slot = &hm->slots[hash % hm->num_slots];

	for (i = 0; i < slot->entries_num; i++)
	{
		if (0 == hm->compare_func(&slot->entries[i].key, &key))
		{
			slot->entries[i] = slot->entries[slot->entries_num - 1];
			slot->entries_num--;
			hm->num_data--;
			break;
		}
	}
}

void	trx_hashmap_clear(trx_hashmap_t *hm)
{
	int	i;

	for (i = 0; i < hm->num_slots; i++)
		hm->slots[i].entries_num = 0;

	hm->num_data = 0;
}
