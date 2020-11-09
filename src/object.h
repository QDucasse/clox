#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

/* Macro to extract the type from the object */
#define OBJ_TYPE(value)        (AS_OBJ(value)->type)
/* Check if the value is a class */
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)
/* Check if the value is an instance */
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
/* Check if the value is a bound method */
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
/* Check if the value is a string */
#define IS_STRING(value)       isObjType(value, OBJ_STRING)
/* Check if the value is a closure */
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
/* Check if the value is a function */
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
/* Check if the value is a native function */
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
/* Conversion to bound method */
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
/* Conversion to class */
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
/* Conversion to instance */
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
/* Conversion value string */
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
/* Conversion value string and outputs the characters */
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)
/* Conversion to closure */
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
/* Conversion to function object */
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
/* Conversion to function object */
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)

typedef enum {
  OBJ_BOUND_METHOD,
  OBJ_CLASS,
  OBJ_INSTANCE,
  OBJ_CLOSURE,
  OBJ_UPVALUE,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING
} ObjType;

/* Structure of an object -> allocated in the heap */
typedef struct sObj {
  ObjType type;
  bool isMarked; /* For the GC */
  struct sObj* next;
} Obj;

/* Representation of a function object in the stack */
typedef struct {
  Obj obj;          /* State of the object, shared among all Objects */
  int arity;        /* Number of arguments */
  int upvalueCount; /* Number of upvalues defined in the function */
  Chunk chunk;      /* Dedicated chunk */
  ObjString* name;  /* Name of the function */
} ObjFunction;

/* Upvalue object */
typedef struct ObjUpvalue {
  Obj obj;                 /* State of the object, shared among all objects */
  Value* location;         /* Location in the stack variable */
  Value closed;            /* Keep the value of the closed upvalue (if it becomes one ) */
  struct ObjUpvalue* next; /* Pointer to the next ObjUpvalue*/
} ObjUpvalue;

/* Representation of a closure */
typedef struct {
  Obj obj;               /* Obj state */
  ObjFunction* function; /* Enclosed function */
  ObjUpvalue** upvalues; /* List of upvalues */
  int upvalueCount;      /* Number of upvalues in the array */
} ObjClosure;

/* Representation of a class */
typedef struct {
  Obj obj;
  ObjString* name;
  Table methods;
} ObjClass;

/* Representation of an instance */
typedef struct {
  Obj obj;
  ObjClass* klass;  /* Pointer to the class it is instance of*/
  Table fields;     /* Instance fields (state etc.) */
} ObjInstance;

/* Representation of a bound method */
typedef struct {
  Obj obj;
  Value receiver;     /* The receiver of the method */
  ObjClosure* method; /* The method to apply to the receiver */
} ObjBoundMethod;

/* Native function */
typedef Value (*NativeFn)(int argCount, Value* args);

/* Native function object */
typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;

/* Structure of a string (object) */
typedef struct sObjString{
  Obj obj;     /* State of the object, shared among all Objects */
  int length;  /* Length of the string */
  char* chars; /* Charaters composing the string */
  uint32_t hash; /* Hash of the string */
} ObjString;

/* Struct inheritance, safe cast from ObjString to Obj (its first field) */

/* New Bound method creation */
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);

/* New class creation */
ObjClass* newClass(ObjString* name);

/* New instance creation */
ObjInstance* newInstance(ObjClass* klass);

/* New function creation */
ObjFunction* newFunction();

/* New upvalue */
ObjUpvalue* newUpvalue(Value* slot);

/* New closure */
ObjClosure* newClosure(ObjFunction* function);

/* New native function */
ObjNative* newNative(NativeFn function);

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
