#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common.h"
#include "src/chunk.h"
#include "src/debug.h"
#include "src/vm.h"

/* ==================================
        EXECUTION METHODS
====================================*/

/* Read Eval Print loop. Interpret line by line */
static void repl() {
  char line[1024];
  for (;;) {
    printf("> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }
    interpret(line);
  }
}
/* Read a file and return the contents in a buffer */
static char* readFile(const char* path) {
  /* Open the file */
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  /* Look for the end of th file */
  fseek(file, 0L, SEEK_END);
  /* Number of bytes since the start of the file */
  size_t fileSize = ftell(file);
  /* Return to the beginning */
  rewind(file);

  /* Allocate enough memory to store the content of the file */
  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  /* Read the bytes from the content of the file */
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytesRead] = '\0';
  fclose(file);
  return buffer;
}

/* Interpret a given file */
static void runFile(const char* path) {
  char* source = readFile(path);
  InterpretResult result = interpret(source);
  free(source);

  if (result == INTERPRET_COMPILE_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

/* ==================================
              MAIN
====================================*/

int main(int argc, const char* argv[]) {
  initVM();
  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: clox [path]\n");
    exit(64);
  }
  freeVM();
  return 0;
}
