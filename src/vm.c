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
  vm.openUpvalues = NULL;
}

/* ==================================
          NATIVE FUNCTION
=================================== */

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
    INITIALIZATION FINALIZATION
=================================== */

/* Initialize the VM by resetting the stack */
void initVM() {
  resetStack();

  vm.objects = NULL;

  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  initTable(&vm.strings);

  vm.initString = NULL;
  vm.initString = copyString("init",4);

  initTable(&vm.globals);
  defineNative("clock",clockNative);
}

/* Free the VM */
void freeVM() {
  freeTable(&vm.strings);
  freeTable(&vm.globals);
  vm.initString = NULL;
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
    ObjFunction* function = frame->closure->function;
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
=================================== */

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
=================================== */
/* Call a function with a number of arguments */
static bool call(ObjClosure* closure, int argCount) {
  /* Check number of arguments */
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d", closure->function->arity, argCount);
    return false;
  }
  /* Check that a deep call chain is not overflowing the callframe array*/
  if(vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }
  /* Sets up the stack to be the function frame */
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  /* redirect the ip on the function call frame */
  frame->ip = closure->function->chunk.code;
  /* Remove the slot containing the function name */
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

/* Check if the callee really is a function */
static bool callValue(Value callee, int argCount) {
  if(IS_OBJ(callee)) {
    switch(OBJ_TYPE(callee)) {
      case OBJ_BOUND_METHOD: {
        ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
        /* Use the 0 slot to hold the receiver*/
        vm.stackTop[-argCount - 1] = bound->receiver;
        return call(bound->method, argCount);
      }
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        /* Create an instance of the class and push it back on
           the stack replacing the class */
        vm.stackTop[-argCount-1] = OBJ_VAL(newInstance(klass));
        /* Look for an init method and call it if found */
        Value initializer;
        if (tableGet(&klass->methods, vm.initString, &initializer)) {
          return call(AS_CLOSURE(initializer), argCount);
        } else if (argCount != 0) {
          runtimeError("Expected 0 arguments but got %d.", argCount);
          return false;
        }
        return true;
      }
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
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
      INVOCATIONS OPERATIONS
=================================== */
/* */
static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
  /* Lookup for the method's name in the method table */
  Value method;
  if(!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  /* Call the method as a closure */
  return call(AS_CLOSURE(method), argCount);
}

/* Define receiver as instance and invoke from class */
static bool invoke(ObjString* name, int argCount) {
  /* Grab receiver */
  Value receiver = peek(argCount);

  /* Check if the receiver is an intance */
  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instance have methods.");
    return false;
  }

  /* Use the instance and invoke the method from the class */
  ObjInstance* instance = AS_INSTANCE(receiver);

  /* check if one of the fields has the same name as a method */
  Value value;
  if(tableGet(&instance->fields, name, &value)) {
    vm.stackTop[-argCount - 1] = value;
    return callValue(value, argCount);
  }

  return invokeFromClass(instance->klass, name, argCount);
}


/* ==================================
      BOUND METHODS OPERATIONS
=================================== */
static bool bindMethod(ObjClass* klass, ObjString* name) {
  /* Look for a method with the given name in the class's method table */
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }
  /* Wrap the found method in a Bound Method object*/
  ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
  /* Pop the instance */
  pop();
  /* Add the bound method to the top */
  push(OBJ_VAL(bound));
  return true;
}

/* ==================================
        UPVALUES OPERATIONS
=================================== */

/* Capture value from the stack and move it to the heap */
static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;         /* Pointer to the upvalue right before the needed one*/
  ObjUpvalue* upvalue = vm.openUpvalues; /* First upvalue on the stack */
  /* Going through the linked list and keeping both the previous upvalue */
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }
  /* If the current upvalue is found in the list, it is returned */
  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  /* The upvalue is unknown and should be created either from an empty ist or next to the previous element */
  ObjUpvalue* createdUpvalue = newUpvalue(local);
  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

/* Close every open upvalue that points to the givn slot or above */
static void closeUpvalues(Value* last) {
  /* Pass through the stack and look over the given location to close upvalues */
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

/* ==================================
         METHOD OPERATIONS
=================================== */
static void defineMethod(ObjString* name) {
  /* Method closure on top of the stack */
  Value method = peek(0);
  /* Class right under the method */
  ObjClass* klass = AS_CLASS(peek(1));
  /* Add the tuple name/method body to the methods table */
  tableSet(&klass->methods, name, method);
  /* Pop the closure */
  pop();
}

/* ==================================
        STRING OPERATIONS
=================================== */
static void concatenate() {
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length+1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  pop();
  pop();
  push(OBJ_VAL(result));
}


/* ==================================
          INTERPRETATION
=================================== */

/* Run a switch over the OPcodes and perform the relative actions with the stack */
static InterpretResult run() {
  /* Top most call frame */
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
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
    disassembleInstruction(&frame->closure->function->chunk,
      (int)(frame->ip - frame->closure->function->chunk.code));
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
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
        break;
      }
      case OP_CLOSE_UPVALUE: {
        closeUpvalues(vm.stackTop -1); /* Close the upvalue -> move its value from the stack to the heap*/
        pop(); /* Discard the stack slot */
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
      case OP_INVOKE : {
        /* Lookup name and number of arguments */
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        if (!invoke(method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        /* Invocation succeeds -> new current frame */
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }

      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        for (int i = 0; i< closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          /* If the value is a local, it is captured into the array */
          /* Else the upvalue is copied from the upper scope */
          if (isLocal) {
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }

      case OP_CLASS:
        push(OBJ_VAL(newClass(READ_STRING())));
        break;
      case OP_METHOD:
        defineMethod(READ_STRING());
        break;

      case OP_GET_PROPERTY: {
        /* Resulting instance is already on top of the stack */
        /* Check if REALLY is an instance */
        if (!IS_INSTANCE(peek(0))) {
          runtimeError("Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjInstance* instance = AS_INSTANCE(peek(0));

        /* Name of the field */
        ObjString* name = READ_STRING();
        /* Look for it in the fields table */
        Value value;
        if (tableGet(&instance->fields, name, &value)) {
          pop(); // Instance
          push(value);
          break;
        }
        /* Look up a method on the receiver */
        if (!bindMethod(instance->klass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
      }

      case OP_SET_PROPERTY: {
        if (!IS_INSTANCE(peek(1))) {
          runtimeError("Only instances have fields.");
          return INTERPRET_RUNTIME_ERROR;
        }
        /* Top of the stack: value THEN instance */
        ObjInstance* instance = AS_INSTANCE(peek(1));
        tableSet(&instance->fields, READ_STRING(), peek(0));

        Value value = pop(); // Stored value
        pop();               // Instance
        push(value);         // Push the value back
        break;
      }

      case OP_RETURN: {
        /* pop the top value */
        Value result = pop();
        /* Close the upvalues in the frame */
        closeUpvalues(frame->slots);
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

  /* Top-level frame setup */
  push(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  callValue(OBJ_VAL(closure), 0); /* Call top level frame */

  return run();
}
