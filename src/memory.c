#include <stdlib.h>

#include "memory.h"
#include "vm.h"

/* ======================
      REALLOCATION
====================== */

/* Memory reallocation */
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  /* Free option */
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }
  /* Allocation options */
  void* result = realloc(pointer, newSize);
  /* In case there isnt enough memory */
  if (result == NULL) exit(1);
  return result;
}

/* ======================
      FREE OBJECTS
====================== */

/* Free an object in the linked list */
static void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      /* Free upvalues array */
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_UPVALUE: {
      FREE(ObjUpvalue, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_NATIVE: {
      FREE(ObjNative, object);
      break;
    }
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
  }
}

/* Free the objects from the linked list */
void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
}
