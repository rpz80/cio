#if !defined (CIO_HASH_SET_H)
#define CIO_HASH_SET_H

/**
 * 'capacity' should be equal to (expected size / 0.75) or more for good hashing results.
 * 'cmp' should return 0 if values are NOT equal, any other number otherwise.
 * 'hash_data' should fill its' arguments 'data' and 'len' for the given 'elem' with correct values
 * of memory area which will be used for calculating hash value.
 * If 'release' is not NULL it will be used to free element pointer. Otherwise you should dispose
 * elements yourself.
 */
void *cio_new_hash_set(unsigned capacity, int (*cmp)(const void *, const void *),
    void (*hash_data)(const void *elem, void **data, int *len), void (*release)(void *));

void cio_free_hash_set(void *set);

/**
 * Returns pointer to the elem if add is successful, NULL otherwise.
 */
void *cio_hash_set_add(void *set, void *elem);

/**
 * Returns pointer to the elem if it is in the set, NULL otherwise.
 */
void *cio_hash_set_get(void *set, const void *elem);

/**
 * Returns pointer to the elem if it is in the set and remove successful, NULL otherwise.
 * Note that this function never frees data pointer, you should dispose it yourself.
 */
void *cio_hash_set_remove(void *set, void *elem);

#endif /* CIO_HASH_SET_H */
