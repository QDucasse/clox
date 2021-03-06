#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

/* Grow the array when it reaches 75% of its capacity */
#define TABLE_MAX_LOAD 0.75

/* Initialize the hash table */
void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

/* Free the hash table */
void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

/* ==================================
            FIND AN ENTRY
====================================*/

/* Find an entry corresponding to a given key, returns a pointer to the entry */
static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  /* Map of the hash to the table using the modulo of the capacity */
  uint32_t index = key->hash % capacity;
  Entry* tombstone = NULL;
  for (;;) {
    /* Return the entry at the given index in the table */
    Entry* entry = &entries[index];

    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        /* Empty entry */
        return tombstone != NULL ? tombstone : entry;
      } else {
        /* Tombstone found */
        if (tombstone == NULL) tombstone = entry;
      }
    } else if (entry->key == key) {
      /* Key found */
      return entry;
    }

    /* Linear probing to look for another place */
    index = (index + 1) % capacity;
  }
}


/* Find a string entry in a string table */
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
  /* Table is empty */
  if (table->count == 0) return NULL;

  uint32_t index = hash % table->capacity;

  for (;;) {
    Entry* entry = &table->entries[index];

    if (entry->key == NULL) {
      /* Stop if we find a non-tombstone entry */
      if (IS_NIL(entry->value)) return NULL;
    } else if (entry->key->length == length &&
               entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, length) == 0) {
      /* The key is found */
      return entry->key;
    }

    index = (index + 1) % table->capacity;
  }
}

/* ==================================
            GC MARKING
====================================*/
/* Remove white nodes from a table (aka unmarked) */
void tableRemoveWhite(Table* table) {
  /* Walk through every entry in the table */
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    /* If the key is not marked delete the entry */
    if (entry->key != NULL && !entry->key->obj.isMarked) {
      tableDelete(table, entry->key);
    }
  }
}

/* Mark all the keys and values in a table */
void markTable(Table* table) {
  /* For all the entries -> Mark key strings and values */
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    markObject((Obj*)entry->key);
    markValue(entry->value);
  }
}


/* ==================================
          SIZE OPERATIONS
====================================*/

/* Allocate an array of entries */
static void adjustCapacity(Table* table, int capacity) {
  Entry* entries = ALLOCATE(Entry, capacity);
  /* Fill the entries in the allocated array with NULL keys and NIL values*/
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  /* Clear the tombstone count */
  table->count = 0;

  /* Report the entries from the old table to the new one */
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key == NULL) continue;

    /* The new table is reused from scratch to handle potential collisaions */
    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++; /* Increment if non-tombstone */
  }

  /* Release the memory from the old array */
  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

/* ==================================
          ADDING ENTRIES
====================================*/

/* Set the value at the given key */
bool tableSet(Table* table, ObjString* key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(table, capacity);
  }
  /* Look for the key in the table */
  Entry* entry = findEntry(table->entries, table->capacity, key);
  /* Check if the key is in the table */
  bool isNewKey = entry->key == NULL;
  /* The count is incremented if the key is new AND it is not overwriting a tombstone*/
  if(isNewKey && IS_NIL(entry->value)) table->count++;

  entry->key = key;
  entry->value = value;
  return isNewKey;
}


/* Copy the content of a hash table into another */
void tableAddAll(Table* from, Table* to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if (entry->key != NULL) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

/* ==================================
          PULLING ENTRIES
====================================*/

/* Get an entry for a given key and store the value in the pointer */
bool tableGet(Table* table, ObjString* key, Value* value) {
  /* The table is empty */
  if(table->count == 0) return false;
  /* Look for the entry corresponding to a given key */
  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

/* ==================================
          DELETING ENTRIES
====================================*/

/* Delete an entry for a given key */
bool tableDelete(Table* table, ObjString* key) {
  /* The table is empty */
  if(table->count == 0) return false;

  /* Find the entry */
  Entry* entry = findEntry(table->entries, table->capacity, key);
  if(entry->key == NULL) return false;

  /* Place a tombstone in the entry */
  entry->key = NULL;
  entry->value = BOOL_VAL(true); /* Tombstone is true */

  return true;
}
