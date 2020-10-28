#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;           /* Chunks to execute */
  uint8_t* ip;            /* instruction pointer (or program counter. Always points to the instruction ABOUT TO BE EXECUTED */
  Value stack[STACK_MAX]; /* Stack of values */
  Value* stackTop;        /* Pointer PAST the top item */
  Table strings;          /* Table of all existing strings in the system */

  Obj* objects;           /* Pointer to the head of the objects linked list */
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
