#if !defined (CIO_HASH_SET_H)
#define CIO_HASH_SET_H

/**
 * 'capacity' should be equal to (expected size / 0.75) or more for good hashing results.
 * 'cmp' should return 0 if values are NOT equal, any other number otherwise.
 * If 'release' is not NULL it will be used to free element pointer. Otherwise you should dispose
 * elements yourself.
 */
void *cio_new_hash_set(unsigned capacity, int (*cmp)(const void *, const void *),
    void (*release)(void *));

void cio_free_hash_set(void *set);

/**
 * Returns pointer to the elem if add is successful, NULL otherwise.
 */
void *cio_hash_set_add(void *set, void *elem, unsigned elem_size);

/**
 * Returns pointer to the elem if it is in the set, NULL otherwise.
 */
void *cio_hash_set_get(void *set, void *elem, unsigned elem_size);

/**
 * Returns pointer to the elem if it is in the set and remove successful, NULL otherwise.
 * Note that this function never frees data pointer, you should dispose it yourself.
 */
void *cio_hash_set_remove(void *set, void *elem, unsigned elem_size);

#endif // CIO_HASH_SET_H
