#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

/* ==================================
        STRUCTS AND GLOBALS
=================================== */

typedef struct {
  Token current;  /* current Token being investigated */
  Token previous; /* next Token being investigated */
  bool hadError;  /* */
  bool panicMode; /* To avoid cascading errors */
} Parser;

/* Parser singleton */
Parser parser;

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
typedef void (*ParseFn)(bool canAssign);

/* Parse Rule */
typedef struct {
  ParseFn prefix;        /* Function to compile a prefix expression starting with a token of that type */
  ParseFn infix;         /* Function to compile an infix expression whose left operand is followed by a token of that type */
  Precedence precedence; /* Precendence of an infix operator that uses that token as an operator */
} ParseRule;

/* Local variable, used in a particular scope */
typedef struct {
  Token name;      /* Identifier lexeme */
  int depth;       /* Scope depth of the block where the local variable was declared */
  bool isCaptured; /* Is currently captured as an upvalue (nested function declaration) */
} Local;

/* Upvalue, used in a scope from an enclosed one */
typedef struct {
  uint8_t index; /* local slot the upvalue is capturing */
  bool isLocal;
} Upvalue;

/* Different types of function */
typedef enum {
  TYPE_FUNCTION,
  TYPE_SCRIPT
} FunctionType;

/* Compiler representation with access to states */
typedef struct Compiler {
  struct Compiler* enclosing; /* Compiler that encloses it */
  ObjFunction* function;      /* Implicit top-level main() function*/
  FunctionType type;          /* Compiler knows when it is in the top level*/

  Local locals[UINT8_COUNT];     /* Array of local variables */
  int localCount;                /* Number of local variables in scope */
  Upvalue upvalues[UINT8_COUNT]; /* Array of upvalues */
  int scopeDepth;                /* Number of blocks surrounding current code */
} Compiler;

/* Compiler global variable */
Compiler* current = NULL;

/* Reference to current chunk */
Chunk* compilingChunk;

/* Return the current chunk */
static Chunk* currentChunk() {
  return &current->function->chunk;
}

/* ==================================
          INITIALIZATION
=================================== */

/* Initialize a compiler and update the global variable */
static void initCompiler(Compiler* compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = newFunction();
  current = compiler;

  if (type != TYPE_SCRIPT) {
    current->function->name = copyString(parser.previous.start, parser.previous.length);
  }

  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  local->name.start = "";
  local->name.length = 0;
}


/* ==================================
          ERROR HANDLING
=================================== */

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
=================================== */

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

/* Checks that the current token is of a givent type */
static bool check(TokenType type) {
  return parser.current.type == type;
}

/* checks if the current token is of a given type */
static bool match(TokenType type) {
  /* If the type isnt correct, the token is NOT consumed */
  if (!check(type)) return false;
  /* Otherwise token consumed */
  advance();
  return true;
}

/* ==================================
        BACK END - BYTECODE
=================================== */

/* ==================================
           BYTE EMISSION
=================================== */

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

/* ==================================
           JUMP EMISSION
=================================== */

/* Emit a jump */
static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  /* Placeholder for the jump address, size 16-bits */
  emitByte(0xff);
  emitByte(0xff);
  /* Return the index of the placeholder */
  return currentChunk()->count - 2;
}

/* */
static void patchJump(int offset) {
  /* -2 to adjust the jump offset itself */
  int jump = currentChunk()->count - offset -2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }
  /* Replace the operand at the given location with the jump offset */
  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

/* ==================================
           LOOP EMISSION
=================================== */

/* Emit a loop body */
void emitLoop(int loopStart) {
  emitByte(OP_LOOP);
  /* Size of the loop _ size of OP_LOOP instruction */
  int offset = currentChunk()->count - loopStart + 2;
  /* Check loop body size */
  if (offset > UINT16_MAX) error("Loop body too large.");
  /* Add the loop body in the chunk */
  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

/* ==================================
           RETURN EMISSION
=================================== */

/* Emit Return opcode */
static void emitReturn() {
  emitByte(OP_NIL);
  emitByte(OP_RETURN);
}

/* ==================================
          CONSTANT EMISSION
=================================== */


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

/* ==================================
            END ROUTINE
=================================== */


/* End routine for the compiler */
static ObjFunction* endCompiler() {
  emitReturn();
  /* Get the current function and return it to compile */
  ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
      /* If the function name is NULL (aka toplevel) return <script> else the function name*/
      disassembleChunk(currentChunk(), function->name != NULL
          ? function->name->chars : "<script>");
    }
#endif

  /* Jump back up one level */
  current = current->enclosing;
  return function;
}

/* ==================================
        SIGNATURES FOR RULES
=================================== */

/* Signature definitions */
static void expression();
static void statement();
static void declaration();
static void variable(bool canAssign);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static void and_(bool canAssign);
static void or_(bool canAssign);
static void dot(bool canAssign);

/* ==================================
          BINARY OPERATION
=================================== */


/* Consumes the operator and the surrounding values */
static void binary(bool canAssign) {
  /* Remember the operator */
  TokenType operatorType = parser.previous.type;
  /* Compile the right operand */
  ParseRule* rule = getRule(operatorType);
  /* The +1 signify our operator is left-associative */
  parsePrecedence((Precedence)(rule->precedence + 1));

  /* Emit the operator instruction */
  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
    default:
      return; /* Unreachable */
  }
}

/* ==================================
          FUNCTION CALL
=================================== */


/* Compile the list of arguments of a function */
static uint8_t argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (argCount == 255) {
        error("Can't have more than 255 arguments.");
      }
      argCount++;
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

/* Compile function call */
static void call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

/* ==================================
              LITERAL
=================================== */


/* Literal definition for nil, false and true */
static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL: emitByte(OP_NIL); break;
    case TOKEN_TRUE: emitByte(OP_TRUE); break;
    default:
      return; // Unreachable.
  }
}

/* ==================================
            GROUPING
=================================== */


/* Consume the expression and the closing parenthesis */
static void grouping(bool canAssign){
  /* Assumes the opening parenthesis has already been consumed */
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}


/* ==================================
              NUMBER
=================================== */


/* Expression for number */
static void number(bool canAssign) {
  /* Assumes the number has already been consumed */
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

/* ==================================
              STRING
=================================== */

/* Creates a string from the token */
static void string(bool canAssign) {
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                  parser.previous.length -2)));
}

/* ==================================
          UNARY OPERATORS
=================================== */

/* Consumes the operand and emit the operator instruction */
static void unary(bool canAssign) {
  /* Leading token consumed and stored */
  TokenType operatorType = parser.previous.type;

  /* Compile the operand */
  parsePrecedence(PREC_UNARY);

  /* Emit the operator instruction AFTER the operand compilation */
  switch (operatorType) {
     case TOKEN_BANG:  emitByte(OP_NOT); break;
     case TOKEN_MINUS: emitByte(OP_NEGATE); break;
     default:
       return; /* Unreachable */
  }
}

/* ==================================
        OPERATOR PRECEDENCE
=================================== */

/* Rules with "element" = prefix rule | infix rule | precedence */
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     dot,    PREC_CALL},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE}
};

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

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  /* If the precedence is lower than the next token's one, parse the infix */
  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

/* Gets the rule with prefix, infix and precedence for a given token type */
static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

/* ==================================
          SYNCHRONIZATION
=================================== */

static void synchronize() {
  parser.panicMode = false;
  /* Look for a statement boundary */
  while (parser.current.type != TOKEN_EOF) {
    /* Look for an end notice */
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    /* Look for the beginning of the next statement */
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:
        return;
      default:
        /* Do nothing */
        ;
    }
  }
}

/* ==================================
            EXPRESSION
=================================== */


/* Expression compilation */
static void expression() {
  parsePrecedence(PREC_ASSIGNMENT);
}

/* ===== IDENTIFIER CREATION AND COMPARISON ===== */

/* Take the given token and add its lexeme to the chunk constant table */
static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

/* Comparison between identifiers */
static bool identifiersEqual(Token* a, Token* b) {
  /* Size comparison */
  if (a->length != b->length) return false;
  /* Lexeme comparison */
  return memcmp(a->start, b->start, a->length) == 0;
}

/* ==================================
         LOCAL VARIABLE
=================================== */

/* Return the index of the local variable in the table */
static int resolveLocal(Compiler* compiler, Token* name) {
  /* Walk the list of local variables */
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    /* If the name corresponds return the index */
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

/* Add a local variable to the list of locals */
static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }
  /* Add the local to the locals array incrementing the index */
  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1; /* flag sentinel as uninitialized*/
  local->isCaptured = false; /* By default living as a simple local */
}

/* ==================================
              UPVALUE
=================================== */

/* Add an upvalue to the closure */
static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
  /* Get the number of upvalues stored in the function */
  int upvalueCount = compiler->function->upvalueCount;

  /* Check if an upvalue with the same index (aka pointing to the ame variable) already exists */
  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  /* Store the upvalue at the given index in the upvalues array */
  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

/* Resolve an upvalue */
static int resolveUpvalue(Compiler* compiler, Token* name) {
  /* Top most level and the value SHOULD be global */
  if(compiler->enclosing == NULL) return -1;
  /* Resolve as a local in the enclosing compiler -> look for it right outside */
  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true; /* The local got captured */
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  /* Look for a matching local variable beyond the immediately enclosing function */
  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if(upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false); /* passes false for isLocal */
  }

  return -1;
}

/* ==================================
         VARIABLE DECLARATION
=================================== */

/* Declare a variable, the compiler records its existence */
static void declareVariable() {
  /* Global variables are implicitly declared */
  if (current->scopeDepth == 0) return;

  /* The compiler records the existence of the variable */
  Token* name = &parser.previous;
  /* Check if two variables have the same name in the same scope */
  for (int i = current->localCount -1; i >= 0; i--) {
    /* Store the ith local */
    Local* local = &current->locals[i];
    /*  If we reach the beginning or a local in another scope, break */
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {

    }
  }

  addLocal(*name);
}

/* Consume an identifier sends it for constant creation */
static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  /* Variable declaration */
  declareVariable();
  /* Exit the function if in local scope */
  if (current->scopeDepth > 0) return 0;
  /* Define a global variable */
  return identifierConstant(&parser.previous);
}

/* Mark a variable as initialized (change the flag -1) */
static void markInitialized() {
  /* Check if we are in the top level */
  if (current->scopeDepth == 0) return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

/* Variable definition */
static void defineVariable(uint8_t global) {
  /* If the variable is a local variable exit as it is already on top of the stack */
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

/* Declare a variable with a given value or nil by default */
static void varDeclaration() {
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration");

  defineVariable(global);
}

/* Take the given identifier and add its lexeme to the chunk constant table */
static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
      getOp = OP_GET_LOCAL;
      setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
      getOp = OP_GET_UPVALUE;
      setOp = OP_SET_UPVALUE;
    } else {
      arg = identifierConstant(&name);
      getOp = OP_GET_GLOBAL;
      setOp = OP_SET_GLOBAL;
    }

  /* Check if the global variable is set or accessed */
  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, (uint8_t)arg);
  } else {
    emitBytes(getOp, (uint8_t)arg);
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

/* ==================================
              AND
=================================== */

/* And compilation as a small if then else clause */
static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  /* Pop the condition */
  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

/* ==================================
              OR
=================================== */

/* Or compilation as a small if then else clause */
static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  /* Second */
  patchJump(endJump);
}

/* ==================================
           DOT (GET SET)
=================================== */

static void dot(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint8_t name = identifierConstant(&parser.previous);
  /* If equal detected -> set, else -> get */
  /* canAssign is here to prevent expressions such as a + b.c = 3*/
  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(OP_SET_PROPERTY, name);
  } else {
    emitBytes(OP_GET_PROPERTY, name);
  }
}

/* ==================================
              BLOCK
=================================== */

/* Notify scope beginning by incrementing depth */
static void beginScope() {
  current->scopeDepth++;
}

/* Notify scope end by decrementing depth and popping the local variables */
static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
           /* If the current local is captured -> closed upvalue, else simply pop */
           if (current->locals[current->localCount - 1].isCaptured) {
             emitByte(OP_CLOSE_UPVALUE);
           } else {
             emitByte(OP_POP);
           }
           current->localCount--;
         }
}

/* Block compilation */
static void block() {
  /* Parse declarations until end of block or end of file */
  while(!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

/* ==================================
              FUNCTION
=================================== */

static void function(FunctionType type) {
  /* A new compiler is created for each function */
  Compiler compiler;
  initCompiler(&compiler, type);
  beginScope();

  /* Compile the parameter list*/
  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }

      uint8_t paramConstant = parseVariable("Expect parameter name.");
      defineVariable(paramConstant);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  /* Compile the body */
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  /* Create the function object */
  ObjFunction* function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

  for (int i = 0; i < function->upvalueCount; i++) {
    /* First byte 1 -> local in the enclosing
                  0 -> upvalue
       Second byte index */
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

/* Function declaration */
static void funDeclaration() {
  /* Works like a global variable */
  uint8_t global = parseVariable("Expect function name.");
  /* Two-phase variable definition can be shortened here */
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}


/* ==================================
            CLASS
=================================== */

static void classDeclaration() {
  /* Add the name around the class */
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();
  /* Create the class object at runtime */
  emitBytes(OP_CLASS, nameConstant);
  defineVariable(nameConstant);
  /* Consume class body */
  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  consume(TOKEN_RIGHT_BRACE, "Expect '}' before class body.");
}

/* ==================================
            DECLARATION
=================================== */

/* Declaration compilation */
static void declaration() {
  if(match(TOKEN_CLASS)) {
    classDeclaration();
  } else if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }


  if (parser.panicMode) synchronize();
}

/* ==================================
           STATEMENTS
=================================== */

/* Compile print */
static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

/* Compile expression statement */
static void expressionStatement() {
  /* Consist of a an expression used for its side effect */
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  /* Result is discarded */
  emitByte(OP_POP);
}

/* Compile if statement */
static void ifStatement() {
  /* Condition expression */
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  /* Create a jump placeholder over the then branch */
  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  /* Add a pop for the condition value */
  emitByte(OP_POP);
  statement();

  /* Create a jump placeholder over the else branch */
  int elseJump = emitJump(OP_JUMP);

  /* Fill the placeholder with the actual value for the THEN branch size */
  patchJump(thenJump);

  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) statement();
  /* Fill the placeholder with the actual value for the THEN branch size */
  patchJump(elseJump);
}

/* While compilation */
static void whileStatement() {
  int loopStart = currentChunk()->count;
  /* Condition */
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition");
  /* Place jump at the beginning of the body */
  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  /* Looped statement */
  emitByte(OP_POP);
  statement();

  emitLoop(loopStart);

  /* Notify the jump to the beginning */
  patchJump(exitJump);
}

/* For loop compilation */
static void forStatement() {
  /* Scope or the for variable */
  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
  /* First loop statement, variable or expression initializer */
  if(match(TOKEN_SEMICOLON)) {
    /* No initializer */
  } else if (match(TOKEN_VAR)) {
    /* Loop iterator declaration */
    varDeclaration();
  } else {
    /* Expression to check */
    expressionStatement();
  }

  int loopStart = currentChunk()->count;
  int exitJump = -1;
  /* If there (i.e. not a single semicolon) */
  if (!match(TOKEN_SEMICOLON)) {
    /* The body has a clause */
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    /* Jump out of the loop if the condition is false */
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); /* Remove the condition from the stack */

  }

  /* Increment compilation */
  /* Single pass means ->
  jump over increment->run body->jump increment->run increment->go next iteration */
  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);

    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'for' clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);

  }
  /* Loop body */
  statement();
  /* Loop to the beginning, no need for placeholder here */
  emitLoop(loopStart);

  /* If there is a condition jump patched condition popped */
  if (exitJump!= -1) {
    patchJump(exitJump);
    emitByte(OP_POP); /* Remove condition from the stack */
  }
  endScope();
}

/* Return compilation */
static void returnStatement() {
  if (current->type == TYPE_SCRIPT) {
    error("Cannot return from top-level code.");
  }
  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}

/* Statement compilation */
static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if(match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

/* ==================================
          COMPILE ROUTINE
=================================== */

/* Convert the tokens in chunks of bytecode */
ObjFunction* compile(const char* source) {
  /* Initialize scanner and current chunk */
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);
  compilingChunk = currentChunk();

  /* Set error and panic mode to false for initialization */
  parser.hadError = false;
  parser.panicMode = false;
  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction* function = endCompiler();
  return parser.hadError ? NULL : function;
}

/* ==================================
          ROOTS MARKING (GC)
=================================== */

/* Mark the roots of the compiler */
void markCompilerRoots() {
  Compiler* compiler = current;
  /* Run through the chain of enclosed compilers and mark their functions */
  while (compiler != NULL) {
    markObject((Obj*)compiler->function);
    compiler = compiler->enclosing;
  }
}
