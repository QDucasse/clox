#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

/* Macro to extract the type from the object */
#define OBJ_TYPE(value)   (AS_OBJ(value)->type)
/* Check for the object value to be string */
#define IS_STRING(value)  isObjType(value, OBJ_STRING)
/* Conversion value string */
#define AS_STRING(value)  ((ObjString*)AS_OBJ(value))
/* Conversion value string and outputs the characters */
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
  OBJ_STRING
} ObjType;

/* Structure of an object -> allocated in the heap */
struct sObj {
  ObjType type;
  struct sObj* next;
};

/* Structure of a string (object) */
struct sObjString {
  Obj obj;     /* State of the object, shared among all Objects */
  int length;  /* Length of the string */
  char* chars; /* Charaters composing the string */
  uint32_t hash; /* Hash of the string */
};

/* Struct inheritance, safe cast from ObjString to Obj (its first field) */

/* Copy a string of a given length and outputs the corresponding ObjString */
ObjString* copyString(const char*, int length);

/* Gain ownership of a string by allocating it */
ObjString* takeString(char* chars, int length);

/* Print an object */
void printObject(Value value);

/* Checks that the value is an object and has the given type */
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
