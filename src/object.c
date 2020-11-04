#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

/* ==================================
          MEMORY ALLOCATION
====================================*/

/* Allocate memory for an object given its type */
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

/* Allocate an object given a size and a type */
static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->isMarked = false;

  /* Add the object to the objects linked list */
  object->next = vm.objects;
  vm.objects = object;

#ifdef DEBUG_LOG_GC
  printf("%p allocate %ld for %d\n", (void*)object, size, type);
#endif

  return object;
}

/* allocate a string object with its length and chars
This function CANNOT take ownership of the characters passed to it */
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  /* Add the string to the table (set) of strings */
  push(OBJ_VAL(string));
  tableSet(&vm.strings, string, NIL_VAL);
  pop();

  return string;
}

/* ==================================
          HASH FUNCTION
====================================*/

/* Compute the hash using the FNV-1a hash function */
static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;

  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }

  return hash;
}

/* ==================================
         FUNCTION CREATION
====================================*/
/* New function creation */
ObjFunction* newFunction() {
  ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);

  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

/* ==================================
         CLOSURE CREATION
====================================*/

ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }
  ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

/* ==================================
         UPVALUE CREATION
====================================*/

ObjUpvalue* newUpvalue(Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->location = slot;
  upvalue->next = NULL;
  upvalue->closed = NIL_VAL;
  return upvalue;
}


/* ==================================
      NATIVE FUNCTION CREATION
====================================*/
ObjNative* newNative(NativeFn function) {
  ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;
  return native;
}

/* ==================================
          STRING CREATION
====================================*/

/* This function claims ownership of the string it is given */
ObjString* takeString(char* chars, int length) {
  /* Compute the hash */
  uint32_t hash = hashString(chars, length);

  /* Look up the string in the table */
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    /* Free the memory taken by the previous string */
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }

  return allocateString(chars, length, hash);
}

/* Create an ObjString with a given string and length */
ObjString* copyString(const char* chars, int length) {
  /* Compute the hash */
  uint32_t hash = hashString(chars, length);

  /* Look if the string has already been interned */
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;

  /* New array allocated on the heap */
  char* heapChars = ALLOCATE(char, length + 1);
  /* Copy of the characters from the lexeme */
  memcpy(heapChars, chars, length);
  /* Add termination character */
  heapChars[length] = '\0';

  return allocateString(heapChars, length, hash);
}

/* ===================================
            PRINT METHODS
====================================*/

/* Function printer */
static void printFunction(ObjFunction* function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}

/* Print object */
void printObject(Value value) {
  switch(OBJ_TYPE(value)) {
    case OBJ_CLOSURE:
      printFunction(AS_CLOSURE(value)->function);
      break;
    case OBJ_UPVALUE:
      printf("upvalue");
      break;
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;
    case OBJ_NATIVE:
      printf("<native fn>");
      break;
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
  }
}
