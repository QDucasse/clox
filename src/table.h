#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

/* Entry representation, key and value */
typedef struct {
  ObjString* key;
  Value value;
} Entry;

/* Table structure, dynamic array of entries */
typedef struct {
  int count;      /* Number of used entries + TOMBSTONES */
  int capacity;   /* Size of the table */
  Entry* entries; /* Actual entries */
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);

ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

#endif
