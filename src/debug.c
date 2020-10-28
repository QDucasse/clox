#include <stdio.h>

#include "debug.h"
#include "value.h"

/* ==================================
        INSTRUCTION HELPERS
====================================*/

/* Simple helper to display the name of the instruction and the offset */
static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

/* Helper to display the name of the constant operator as well as the constant */
static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset+1];
  /* The print objective is to print for example
     0000 OP_CONSTANT     0 '1.34' */
  printf("%-16s %4d '", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  /* needs to hop over the opcode + index */
  return offset + 2;
}

/* ==================================
        DISASSEMBLER ROUTINE
====================================*/

/* Disassemble instructions one by one in a given chunk */
void disassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);
  int offset;
  for (offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset);
  }
}

/* Disassemble an instruction by testing the instruction against the known ones*/
int disassembleInstruction(Chunk* chunk, int offset) {
  /* Print the offset */
  printf("%04d ", offset);

  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset-1]) {
    printf("   | ");
  } else {
    printf("%4d ", chunk->lines[offset]);
  }
  /* Switch on the instruction with helper simpleInstruction() */
  uint8_t instruction = chunk->code[offset];
  switch (instruction) {

    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);

    case OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);

    case OP_EQUAL:
      return simpleInstruction("OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction("OP_GREATER", offset);
    case OP_LESS:
      return simpleInstruction("OP_LESS", offset);

    case OP_ADD:
      return simpleInstruction("OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction("OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction("OP_MULTIPLY", offset);
    case OP_DIVIDE:
      return simpleInstruction("OP_DIVIDE", offset);

    case OP_NOT:
      return simpleInstruction("OP_NOT", offset);
    case OP_NEGATE:
      return simpleInstruction("OP_NEGATE", offset);

    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);

    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}
