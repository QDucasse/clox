#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

/* Chunk Initialization */
void initChunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
  /* Free the array "code" */
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  /* Free the array "lines" */
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  /* Remove the count and capacity */
  initChunk(chunk);
  /* Free the constants list */
  freeValueArray(&chunk->constants);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
  /* Check if the current array gas room for a new byte */
  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;
    /* Figure out the new capacity */
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    /* Grow the array for the amount of capacity */
    chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    /* Grow the line array as well */
    chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
  }

  /* Add the byte of code to the chunk */
  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

/* Adds a constant to the constant pool and returns its index */
int addConstant(Chunk* chunk, Value value) {
  writeValueArray(&chunk->constants, value);
  return chunk->constants.count - 1;
}
