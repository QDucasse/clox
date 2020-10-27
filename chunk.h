#ifndef clox_chunk_h
#define clox_chunk_h
/* Bytecodes representations */
#include "common.h"
#include "value.h"

typedef enum {
  OP_CONSTANT,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NEGATE,
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