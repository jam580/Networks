#include <stdlib.h>
#include <assert.h>
#include "mem.h"
#include "failure.h"

/*
 * Function: Mem_ensure_allocated
 * ----------------------------
 *   Ensures that ptr is not null, exiting the program with failure if it is
 *
 *   ptr: a pointer or NULL
 */
void Mem_ensure_allocated(void *ptr)
{
    if (ptr == NULL)
        exit_failure("Memory allocation failed.\n");
}

/*
 * Function: Mem_alloc
 * ----------------------------
 *   Allocates nbytes and returns a pointer to the first byte. The bytes
 *   are uninitialized. It is a checked runtime error for nbytes to be
 *   nonpositive. The program exits if the memory cannot be allocated.
 *
 *   nbytes: positive integer-like value of bytes to allocate
 *
 *   returns: pointer to the first byte of the allocated nbytes
 */
void *Mem_alloc(long nbytes)
{
    void *ptr;
    assert(nbytes > 0);
    ptr = malloc(nbytes);
    Mem_ensure_allocated(ptr);
    return ptr;
}

/*
 * Function: Mem_calloc
 * ----------------------------
 *   Allocates a block large enough to hold an array of count elements each of 
 *   size nbytes and returns a pointer to the first element. The block is
 *   initialized to zeros. It is a checked runtime error for count or nbytes
 *   to be nonpositive. The program exits if the memory cannot be allocated.
 *
 *   count: positive integer-like number of elements to allocate
 *   nbytes: positive integer-like value of bytes to allocate for each element
 *
 *   returns: pointer to the first element of the allocated array
 */
void *Mem_calloc(long count, long nbytes) 
{
    void *ptr;
    assert(count > 0);
    assert(nbytes > 0);
    ptr = calloc(count, nbytes);
    Mem_ensure_allocated(ptr);
    return ptr;
}

/*
 * Function: Mem_free
 * ----------------------------
 *   Deallocates memory at ptr. It is an unchecked runtime error to free a
 *   pointer not previous allocated with Mem_alloc, Mem_calloc, or Mem_resize.
 *
 *   ptr: the pointer to deallocate
 */
void Mem_free(void *ptr) 
{
    if (ptr)
        free(ptr);
}

/*
 * Function: Mem_resize
 * ----------------------------
 *   Changes the size of the block at ptr, previously allocated with Mem_alloc,
 *   Mem_calloc, or Mem_resize. Exits the program if memory cannot be allocated.
 *   If nbytes exceeds the size of the blocked pointed to by ptr, the excess
 *   bytes are uninitialized. Otherwise, nbytes beginning at ptr are copied to
 *   the new block. It is a checked runtime error to pass a null ptr and for
 *   nbytes to be nonpositive. It is an unchecked runtime error to pass a ptr
 *   that was not previously allocated by Mem_alloc, Mem_calloc, or Mem_resize.
 *
 *   ptr: non-null pointer to resize
 *   nbytes: positive number of bytes to resize ptr to (expand or contract)
 * 
 *   returns: pointer to the first byte of the resized block.
 */
void *Mem_resize(void *ptr, long nbytes) 
{
    assert(ptr);
    assert(nbytes > 0);
    ptr = realloc(ptr, nbytes);
    Mem_ensure_allocated(ptr);
    return ptr;
}