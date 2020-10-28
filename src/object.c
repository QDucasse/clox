#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

/* Allocate memory for an object given its type */
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

/* Allocate an object given a size and a type */
static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;

  /* Add the object to the objects linked list */
  object->next = vm.objects;
  vm.objects = object;
  return object;
}

/* allocate a string object with its length and chars
This function CANNOT take ownership of the characters passed to it */
static ObjString* allocateString(char* chars, int length) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;

  return string;
}

/* This function claims ownership of the string it is given */
ObjString* takeString(char* chars, int length) {
  return allocateString(chars, length);
}

/* Create an ObjString with a given string and length */
ObjString* copyString(const char* chars, int length) {
  /* New array allocated on the heap */
  char* heapChars = ALLOCATE(char, length + 1);
  /* Copy of the characters from the lexeme */
  memcpy(heapChars, chars, length);
  /* Add termination character */
  heapChars[length] = '\0';

  return allocateString(heapChars, length);
}

/* Print object */
void printObject(Value value) {
  switch(OBJ_TYPE(value)) {
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
  }
}
