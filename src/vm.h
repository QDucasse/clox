#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"
#include "object.h"
#include "table.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  ObjClosure* closure;   /* Reference to the enclosed closure */
  uint8_t* ip;           /* Own Instruction pointer */
  Value* slots;          /* First available slot in the VM's value stack */
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX]; /* Frame array treated as a stack */
  int frameCount;               /* Number of frames in the call frame */

  Chunk* chunk;             /* Chunks to execute */
  uint8_t* ip;              /* instruction pointer (or program counter. Always points to the instruction ABOUT TO BE EXECUTED */
  Value stack[STACK_MAX];   /* Stack of values */
  Value* stackTop;          /* Pointer PAST the top item */
  ObjString* initString;    /* 'init' string hardcoded for faster lookup */
  ObjUpvalue* openUpvalues; /* Linked list of open upvalues */

  Table strings;            /* Table of all existing strings in the system */
  Table globals;            /* Table of all existing globals in the system */

  Obj* objects;           /* Pointer to the head of the objects linked list */

  size_t bytesAllocated; /* Total bytes allocated by the VM */
  size_t nextGC;         /* Threshld to trigger the next collection */

  int grayCount;    /* Count of gray objects */
  int grayCapacity; /* Max number of gray objects in the stack*/
  Obj** grayStack;  /* Stack of gray objects */
} VM;

/* Chunk interpretation results */
typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

void push(Value value);
Value pop();

#endif
