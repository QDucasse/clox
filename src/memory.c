#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

/* ======================
      REALLOCATION
====================== */

/* Memory reallocation */
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated+= newSize - oldSize;

  /* Force a collection to run during reallocation for memory acquisition if the flag is set */
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif

    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
  }
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
#ifdef DEBUG_LOG_
  printf("%p free type %d\n", (void*)object, object->type);
#endif
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

/* Free the objects from the vm objects linked list */
void freeObjects() {
  /* Free VM Objects */
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
  /* Free VM gray objects (GC) */
  free(vm.grayStack);
}

/* ======================
   GARBAGE COLLECTION
====================== */

/* Mark an object or the GC */
void markObject(Obj* object) {
  if(object == NULL) return;
  if(object->isMarked) return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  object->isMarked = true;

  if(vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
    /* If the gray stack cannot grow, the GC simply abort */
    if (vm.grayStack == NULL) exit(1);
  }

  vm.grayStack[vm.grayCount++] = object;
}


/* Mark a value for the GC */
void markValue(Value value) {
  if (!IS_OBJ(value)) return;
  markObject(AS_OBJ(value));
}


/* Mark an array of values */
static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}


/* Remove an object from the gray stack and mark the values it references */
static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      /* Mark the enclosed function */
      markObject((Obj*)closure->function);
      /* Mark the upvalues in the closure */
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject((Obj*)closure->upvalues[i]);
      }
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      /* Mark the function name */
      markObject((Obj*)function->name);
      /* Mark the tabe of references to other objects */
      markArray(&function->chunk.constants);
      break;
    }
    case OBJ_UPVALUE:
      /* Mark the closed-over value */
      markValue(((ObjUpvalue*)object)->closed);
      break;
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}


/* Mark the roots of the program */
static void markRoots() {
  /* Mark all the values kept in the stack */
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }
  /* Mark all the closures extracted from the stack of callframes */
  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }
  /* Mark all the open upvalues */
  for (ObjUpvalue* upvalue = vm.openUpvalues;
       upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }
  /* Mark all the global variables*/
  markTable(&vm.globals);
  /* Mark the roots of the compiler */
  markCompilerRoots();
}

/* Trace the references by going through gray objects and blacken them */
static void traceReferences() {
  /* Run through the gray objects and blacken them */
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[vm.grayCount--];
    blackenObject(object);
  }
}


/* Remove the variables that are not used (white nodes) */
static void sweep() {
  Obj* previous = NULL;
  Obj* object = vm.objects;
  /* Walk the linked list of every object in the heap */
  while (object != NULL) {
    /* If the object is marked, process to the next */
    if (object->isMarked) {
      /* Remove the mark once processed */
      object->isMarked = false;
      previous = object;
      object = object->next;
    } else {
      /* If an object is unmarked -> unlink from the list */
      Obj* unreached = object;

      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}

/* Trigger a garbage collection pass */
void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  /* Mark the roots of the program */
  markRoots();
  /* Trace the roots' references */
  traceReferences();
  /* Between marking and sweeping, check strings (weak references) */
  tableRemoveWhite(&vm.strings);
  /* Remove the unused references */
  sweep();
  /* Chose when to trigger the next GC */
  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %ld bytes (from %ld to %ld) next at %ld\n",
       before - vm.bytesAllocated, before, vm.bytesAllocated,
       vm.nextGC);
#endif
}
