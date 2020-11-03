#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

/* Use a global for the VM object it can be better to use pointers everywhere. */
VM vm;

/* No allocation so resetting the stack simply moves the initial pointer
and overwrites the data in the stack */
static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
}

/* ==================================
          NATIVE FUNCTION
====================================*/

/* Define a native function through the stack so everything can get gcd */
static void defineNative(const char* name, NativeFn function) {
  /* The two values are pushed set then popped this is for the GC */
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

/* Native function clock */
static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

/* ==================================
          INITIALIZATION
====================================*/

/* Initialize the VM by resetting the stack */
void initVM() {
  resetStack();
  vm.objects = NULL;
  initTable(&vm.strings);
  initTable(&vm.globals);
  defineNative("clock",clockNative);
}

/* Free the VM */
void freeVM() {
  freeTable(&vm.strings);
  freeTable(&vm.globals);
  freeObjects();
}

/* Runtime error reporting */
static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->function;
    // -1 because the IP is sitting on the next instruction to be
    // executed.
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ",
            function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
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

/* Look for a value that is distance far down from the top but DOES NOT pop it */
static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

/* Determine falsiness -> nil and false = FALSE while everythin = TRUE */
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

/* ==================================
              CALLS
====================================*/
/* Call a function with a number of arguments */
static bool call(ObjFunction* function, int argCount) {
  /* Check number of arguments */
  if (argCount != function->arity) {
    runtimeError("Expected %d arguments but got %d", function->arity, argCount);
    return false;
  }
  /* Check that a deep call chain is not overflowing the callframe array*/
  if(vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }
  /* Sets up the stack to be the function frame */
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  /* redirect the ip on the function call frame */
  frame->ip = function->chunk.code;
  /* Remove the slot containing the function name */
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

/* Check if the callee reslly is a function */
static bool callValue(Value callee, int argCount) {
  if(IS_OBJ(callee)) {
    switch(OBJ_TYPE(callee)) {
      case OBJ_FUNCTION:
        return call(AS_FUNCTION(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        Value result = native(argCount, vm.stackTop - argCount);
        vm.stackTop -= argCount + 1;
        push(result);
        return true;
      }
      default:
        /* Non-callable object type */
        break;
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}

/* ==================================
        STRING OPERATIONS
====================================*/
static void concatenate() {
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length+1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  push(OBJ_VAL(result));
}


/* ==================================
          INTERPRETATION
====================================*/

/* Run a switch over the OPcodes and perform the relative actions with the stack */
static InterpretResult run() {
  /* Top most call frame */
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8 | frame->ip[-1])))
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
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
    disassembleInstruction(&frame->function->chunk, (int)(frame->ip - frame->function->chunk.code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {

      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }

      case OP_NIL:   push(NIL_VAL); break;
      case OP_TRUE:  push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;

      case OP_POP: pop(); break;

      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }

      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        /* tableSet returns true if a new key is given */
        if (tableSet(&vm.globals, name, peek(0))) {
          /* Remove the entry created */
          tableDelete(&vm.globals, name);
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, name, peek(0));
        pop();
        break;
      }

      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS:    BINARY_OP(BOOL_VAL, <); break;

      case OP_ADD:      {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or two strings");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;

      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;
      case OP_NEGATE:
        if(!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        /* Unwrap negate wrap and push */
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;

      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }

      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }

      case OP_CALL: {
        int argCount = READ_BYTE();
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }

      case OP_RETURN: {
        /* pop the top value */
        Value result = pop();
        /* exit the function frame */
        vm.frameCount--;
        /* If in the top frame, end of the program */
        if(vm.frameCount == 0) {
          pop(); /* Pop main script function */
          return INTERPRET_OK;
        }

        /* Discard the slots the callee was using for its parameters */
        vm.stackTop = frame->slots;
        /* Push the return value on the stac */
        push(result);
        /* Go up one frame */
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
}

/* Interpret a chunk by setting the ip on top of the chunk code then running the interpret */
InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);
  /* Compile function had an error */
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));         /* Top-level frame setup */
  callValue(OBJ_VAL(function), 0); /* Call top level frame */
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slots = vm.stack;

  return run();
}
