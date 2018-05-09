#if !defined (CIO_HASH_SET_H)
#define CIO_HASH_SET_H

void *cio_new_hash_set(int capacity, int (*cmp)(const void *, const void *));
void cio_free_hash_set(void *set);

/**
 * Returns pointer to the elem if add is successful, NULL otherwise.
 */
void *cio_hash_set_add(void *set, void *elem);

/**
 * Returns pointer to the elem if it is in the set, NULL otherwise.
 */
void *cio_hash_set_get(void *set, void *elem);

/**
 * Returns pointer to the elem if it is in the set and remove successful, NULL otherwise.
 */
void *cio_hash_set_remove(void *set, void *elem);

#endif // CIO_HASH_SET_H
