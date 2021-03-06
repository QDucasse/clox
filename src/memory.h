#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

/* Allocate an array with a given element type and count */
#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

/*
The capacity becomes either 8 if the old one was smaller
or doubles the amount. 8 is arbitrary but should be looked
against real-world usage.
*/
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

/* Extends an array of "type" */
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
        sizeof(type) * (newCount))

/* Frees the content of an array, uses the option to pass 0 to the
new size for reallocate() */
#define FREE_ARRAY(type, pointer, oldCount) \
  reallocate(pointer, sizeof(type) * (oldCount), 0)

/*
Only function in the VM that allocates memory, frees it and
changes the size of allocated chunks. It knows what operation
is going on through:
oldSize | newSize  | Operation
--------+----------+-----------------------------
0       | non-0    | Allocate new block
non-0   | 0        | Free allocation
non-0   | <oldSize | Shrink existing allocation
non-0   | >oldSize | Grow existing allocation
*/
void* reallocate(void* pointer, size_t oldSize, size_t newSize);
/* GC main function */
void collectGarbage();
/* Mark a value for the GC - Test if object and pass to markObject */
void markValue();
/* Mark an object for the GC*/
void markObject();
/* Free the objects in the linked list */
void freeObjects();

#endif
