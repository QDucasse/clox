#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
  const char* start;   /* Start of the lexem being scanned */
  const char* current; /* Current character being scanned */
  int line;            /* Line number for error reporting */
} Scanner;

Scanner scanner;

void initScanner(const char* source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

/* ==================================
          CHARACTER TESTS
====================================*/

/* Check if the current character is EOF */
static bool isAtEnd() {
  return *scanner.current == '\0';
}

/* Check if the current character is a letter */
static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
          c == '_';
}

/* Check if the current character is a digit */
static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

/* ==================================
            PEEK ROUTINE
====================================*/

/* Consume the current character and return it */
static char advance() {
  scanner.current++;
  return scanner.current[-1];
}

/* Return the current scanned character */
static char peek() {
  return *scanner.current;
}

/* Return the character next to the currently scanned one */
static char peekNext() {
  if (isAtEnd()) return '\0';
  return scanner.current[1];
}

/* ==================================
          TOKEN CREATION
====================================*/

/* Create a token from a type */
static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);
  token.line = scanner.line;

  return token;
}

/* Create an error token with a message and the line */
static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;

  return token;
}

/* Skip all whitspace characters */
static void skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      /* carriage return */
      case '\n':
        scanner.line++;
        advance();
        break;
      /* comment */
      case '/':
        if (peekNext() == '/') {
          /* A comment goes until the end of the line. */
          while (peek() != '\n' && !isAtEnd()) advance();
        } else {
          return;
        }
        break;

      default:
        return;
    }
  }
}

/* Check if a word is a reserved keyword */
static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
  /* Test if :
  - the lexeme is exactly as long as the reserved word
  - the characters are corresponding */
  if (scanner.current - scanner.start == start + length &&
      memcpy(scanner.start + start, rest, length) == 0) {
    return type;
  }

  return TOKEN_IDENTIFIER;
}

/* Define the identifier type */
static TokenType identifierType() {
  switch (scanner.start[0]) {
    case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
    case 'c': return checkKeyword(1, 4, "lass", TOKEN_CLASS);
    case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
          case 'u': return checkKeyword(2, 1, "n",TOKEN_FUN);
        }
      }
    case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
    case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
    case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
    case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
    case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
          case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
        }
      }
    case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
    case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);


  }
  return TOKEN_IDENTIFIER;
}

/* Create an identifier token */
static Token identifier() {
  /* Numbers are also allowed after the first letter */
  while (isAlpha(peek()) || isDigit(peek())) advance();
  return makeToken(identifierType());
}

/* Create a number token */
static Token number() {
  while(isDigit(peek())) advance();
  /* Look for a decimal part */
  if (peek() == '.' && isDigit(peekNext())) {
    /* Consume the . */
    advance();
  }

  return makeToken(TOKEN_NUMBER);
}

/* Create a string token */
static Token string() {
  while (peek() != '"' && !isAtEnd()) {
    /* String with a carriage return inside it */
    if (peek() == '\n') scanner.line++;
    advance();
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  /* Pass the closing quote with a last call to advance() */
  advance();
  return makeToken(TOKEN_STRING);
}


/* Check that the current character is the expected */
static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;
  /* Increment the counter */
  scanner.current++;
  return true;
}

/* Scan the current lexeme into a token */
Token scanToken() {
  skipWhitespace();

  scanner.start = scanner.current;

  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();
  /* Identifier */
  if (isAlpha(c)) return identifier();
  /* Digit */
  if (isDigit(c)) return number();

  switch (c) {
    /* Single character */
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': return makeToken(TOKEN_LEFT_BRACE);
    case '}': return makeToken(TOKEN_RIGHT_BRACE);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': return makeToken(TOKEN_DOT);
    case '-': return makeToken(TOKEN_MINUS);
    case '+': return makeToken(TOKEN_PLUS);
    case '/': return makeToken(TOKEN_SLASH);
    case '*': return makeToken(TOKEN_STAR);
    /* Double character */
    case '!':
      return makeToken(
          match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      return makeToken(
          match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      return makeToken(
          match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return makeToken(
          match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    /* Literal tokens */
    case '"': return string();
  }


  return errorToken("Unexpected character.");
}
