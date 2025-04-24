#ifndef SQL_INDEXER_H
#define SQL_INDEXER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

// --- Constants ---
extern const char *CREATE_TABLE_KEYWORD;
extern const size_t CREATE_TABLE_LEN;
extern const size_t CHUNK_SIZE;
extern const size_t BUFFER_EXTRA_MARGIN;

// --- Parser State Enum ---
typedef enum {
    STATE_CODE,             // Default state, outside comments/strings
    STATE_SL_COMMENT,       // Single-line comment (-- or #)
    STATE_ML_COMMENT,       // Multi-line comment (/* */)
    STATE_S_QUOTE_STRING,   // Single-quoted string ('...')
    STATE_D_QUOTE_STRING,   // Double-quoted string ("...")
    STATE_BACKTICK_IDENTIFIER // Backtick-quoted identifier (`...`)
} ParserState;

// --- Data Structures ---

// Structure to hold one index entry
typedef struct {
    long long offset;   // Byte offset in the file where "CREATE TABLE" starts
    long long line;     // 1-based line number
    long long column;   // 1-based column number
    char *name;         // Dynamically allocated table name
} TableEntry;

// Structure for the dynamic array of index entries
typedef struct {
    TableEntry *entries;
    size_t count;       // Number of entries currently stored
    size_t capacity;    // Allocated capacity of the entries array
} TableIndex;

// Structure to hold the overall parsing context
typedef struct {
    FILE *fp;                   // File pointer for the SQL file
    char *buffer;               // Read buffer
    size_t buffer_alloc_size;   // Allocated size of the buffer
    size_t buffer_data_len;     // Current amount of valid data in the buffer
    long long global_offset;    // Global byte offset corresponding to buffer start
    long long current_line;     // Current 1-based line number
    long long last_newline_offset; // Global offset of the last encountered newline
    ParserState state;          // Current parser state
    TableIndex index;           // The index being built
    bool error_occurred;        // Flag if a fatal error happened
} ParsingContext;

// --- Function Declarations ---

// Initialize the parsing context (opens file, allocates buffer)
bool initialize_context(ParsingContext *ctx, const char *filename);

// Free resources held by the context (closes file, frees memory)
void cleanup_context(ParsingContext *ctx);

// Main loop for reading and processing the file
bool process_sql_file(ParsingContext *ctx);

// Print the indexed results
void print_results(const TableIndex *index);

#endif // SQL_INDEXER_H
