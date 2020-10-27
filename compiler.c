#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

/* ==================================
        STRUCTS AND GLOBALS
====================================*/

typedef struct {
  Token current;  /* current Token being investigated */
  Token previous; /* next Token being investigated */
  bool hadError;  /* */
  bool panicMode; /* To avoid cascading errors */
} Parser;

/* Operator Precedence from lowest to highest */
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  /* =         */
  PREC_OR,          /* or        */
  PREC_AND,         /* and       */
  PREC_EQUALITY,    /* == !=     */
  PREC_COMPARISON,  /* < > <= >= */
  PREC_TERM,        /* + -       */
  PREC_FACTOR,      /* * /       */
  PREC_UNARY,       /* ! -       */
  PREC_CALL,        /* . ()      */
  PREC_PRIMARY
} Precedence;

/*Type def for a function type that takes no argument and returns nothing */
typedef void (*ParseFn)();

/* Parse Rule */
typedef struct {
  ParseFn prefix;        /* Function to compile a prefix expression starting with a token of that type */
  ParseFn infix;         /* Function to compile an infix expression whose left operand is followed by a token of that type */
  Precedence precedence; /* Precendence of an infix operator that uses that token as an operator */
} ParseRule;

ParseRule rules[];

/* Parser singleton */
Parser parser;

/* Reference to the current Chunk */
Chunk* compilingChunk;

/* Return the current chunk */
static Chunk* currentChunk() {
  return compilingChunk;
}

/* ==================================
          ERROR HANDLING
====================================*/

/* Notifies the error with a message */
static void errorAt(Token* token, const char* message) {
  /* if PANIC MODE already triggered */
  if (parser.panicMode) return;

  /* Enter PANIC MODE */
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    /* Error at the end of the file*/
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    /*Nothing, actual error */
  } else {
    /* Print the token location where the error occured */
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  /* Print the actual error message */
  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

/* Notifies an error in the token just processed */
static void error(const char* message) {
  errorAt(&parser.previous, message);
}

/* Notifies an error in the current token */
static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

/* ==================================
        FRONT END - PARSING
====================================*/

/* Advance the parser with a new non-error token handed over by the scanner */
static void advance() {
  parser.previous = parser.current;

  /* Keep reading until it finds a non-error token */
  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

/* Expects the next token to be of a given type, else errors with given message */
static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }
  errorAtCurrent(message);
}

/* ==================================
        BACK END - BYTECODE
====================================*/

/* ======= BYTES ========= */

/* Add a byte to the current chunk */
static void emitByte(uint8_t byte) {
  /* The previous line number is used so that
     runtime errors are associated with that line */
  writeChunk(currentChunk(), byte, parser.previous.line);
}

/* Helper function for OPCODE followed by one-byt operand */
static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

/* ========= OPERATOR PRECEDENCE =========== */

/* Gets the rule with prefix, infix and precedence for a given token type */
static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

/* Parse the precedence of operators as defined in the above enumeration */
static void parsePrecedence(Precedence precedence) {
  /* Consume the first token of a given precedence */
  advance();
  /* Parse the prefix. It HAS to be there by definition or it is a syntax error */
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  /* Apply the prefix parser, might be nested */
  prefixRule();

  /* If the precedence is lower than the next token's one, parse the infix */
  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule();
  }

}

/* ========== EXPRESSION ============= */

/* Expression compilation */
static void expression() {
  parsePrecedence(PREC_ASSIGNMENT);
}


/* ======= NUMBER ========= */

/* Create constant byte from value */
static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk");
    return 0;
  }

  return (uint8_t)constant;
}

/* Emit a constant OPCODE */
static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

/* Expression for number */
static void number() {
  /* Assumes the number has already been consumed */
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

/* ======= RETURN ========= */

/* Emit Return opcode */
static void emitReturn() {
  emitByte(OP_RETURN);
}

/* End routine for the compiler */
static void endCompiler() {
  emitReturn();
}

/* ======== GROUPING ========== */

/* Consume the expression and the closing parenthesis */
static void grouping(){
  /* Assumes the opening parenthesis has already been consumed */
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

/* ======== UNARY NEGATION ========== */

/* Consumes the operand and emit the operator instruction */
static void unary() {
  /* Leading token consumed and stored */
  TokenType operatorType = parser.previous.type;

  /* Compile the operand */
  parsePrecedence(PREC_UNARY);

  /* Emit the operator instruction AFTER the operand compilation */
  switch (operatorType) {
     case TOKEN_MINUS: emitByte(OP_NEGATE); break;
     default:
       return; /* Unreachable */
  }
}

/* ======== BINARY OPERATION ========== */

/* Consumes the operator and the surrounding values */
static void binary() {
  /* Remember the operator */
  TokenType operatorType = parser.previous.type;
  /* Compile the right operand */
  ParseRule* rule = getRule(operatorType);
  /* The +1 signify our operator is left-associative */
  parsePrecedence((Precedence)(rule->precedence + 1));

  /* Emit the operator instruction */
  switch (operatorType) {
    case TOKEN_PLUS:   emitByte(OP_ADD); break;
    case TOKEN_MINUS:  emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:   emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:  emitByte(OP_DIVIDE); break;
    default:
      return; /* Unreachable */
  }
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_GREATER]       = {NULL,     NULL,   PREC_NONE},
  [TOKEN_GREATER_EQUAL] = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LESS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LESS_EQUAL]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_STRING]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE}
};

/* ==================================
          COMPILE ROUTINE
====================================*/

/* Convert the tokens in chunks of bytecode */
bool compile(const char* source, Chunk* chunk) {
  /* Initialize scanner and current chunk */
  initScanner(source);
  compilingChunk = chunk;

  /* Set error and panic mode to false for initialization */
  parser.hadError = false;
  parser.panicMode = false;
  advance();
  expression();
  consume(TOKEN_EOF, "Expect end of expression.");

  endCompiler();
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
      disassembleChunk(currentChunk(), "code");
    }
#endif
  return !parser.hadError;
}
