#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

/* Use a global for the VM object it can be better to use pointers everywhere. */
VM vm;

/* No allocation so resetting the stack simply moves the initial pointer
and overwrites the data in the stack */
static void resetStack() {
  vm.stackTop = vm.stack;
}

/* Initialize the VM by resetting the stack */
void initVM() {
  resetStack();
}

/* Free the VM */
void freeVM() {
}

/* ==================================
          STACK OPERATIONS
====================================*/

/* Add a value on top of the stack */
void push(Value value) {
  *vm.stackTop = value; /* Store the value on top of the stack */
  vm.stackTop++;        /* Move the top pointer */
}

/* Pop the top value and returns it */
Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

/* ==================================
          INTERPRETATION
====================================*/

/* Run a switch over the OPcodes and perform the relative actions with the stack */
static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op) \
    do { \
      double b = pop(); \
      double a = pop(); \
      push(a op b); \
    } while (false)


  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    Value* slot;
    for (slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {

      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }

      case OP_ADD:      BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE:   BINARY_OP(/); break;

      case OP_NEGATE: push(-pop()); break;

      case OP_RETURN: {
        printValue(pop());
        printf("\n");
        return INTERPRET_OK;
      }
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

/* Interpret a chunk by setting the ip on top of the chunk code then running the interpret */
InterpretResult interpret(const char* source) {
  Chunk chunk;
  initChunk(&chunk);

  if (!compile(source, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_COMPILE_ERROR;
  }

  vm.chunk = &chunk;
  vm.ip = vm.chunk->code;

  InterpretResult result = run();

  freeChunk(&chunk);
  return result;
}
