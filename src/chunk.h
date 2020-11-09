#ifndef clox_chunk_h
#define clox_chunk_h
/* Bytecodes representations */
#include "common.h"
#include "value.h"

typedef enum {
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_SET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_NEGATE,
  OP_PRINT,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_CLASS,
  OP_RETURN
} OpCode;

typedef struct { /* Dynamic array */
  int count;     /* Number of allocated entries in use */
  int capacity;  /* Number of elements allocated */
  uint8_t* code; /* Instruction Array */
  ValueArray constants; /* Constants array */
  int* lines; /* Line number of the corresponding byte */
} Chunk;

/* Chunk Initialization */
void initChunk(Chunk* chunk);
/* Free a chunk */
void freeChunk(Chunk* chunk);
/* Append a byte to the end of the chunk, memorize the line number it comes from */
void writeChunk(Chunk* chunk, uint8_t byte, int line);
/* Add a constant to the constant pool of the chunk */
int addConstant(Chunk* chunk, Value value);

#endif
