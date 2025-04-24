#ifndef SQL_INDEXER_H
#define SQL_INDEXER_H

#include <stdio.h>
#include <stdbool.h> // Include for bool type
#include <stddef.h> // Include for size_t

// --- Global Verbose Flag ---
extern bool verbose_mode;

// --- Debug Macro ---
#define DEBUG_PRINT(fmt, ...) \
    do { if (verbose_mode) fprintf(stderr, "[DEBUG] %s:%d:%s(): " fmt "\n", \
                                __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

// --- Constants ---
// extern const char *CREATE_TABLE_KEYWORD; // Defined in .c
// extern const size_t CREATE_TABLE_LEN; // Defined in .c
// extern const size_t CHUNK_SIZE; // Defined in .c
// extern const size_t BUFFER_EXTRA_MARGIN; // Defined in .c

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

// Structure to hold column information
typedef struct {
    char *name;
    char *type;
    bool is_primary_key;
    bool is_not_null;
    bool is_auto_increment;
    char *default_value;
} ColumnInfo;

// Structure to hold table information with columns
typedef struct {
    char *name;
    ColumnInfo *columns;
    int column_count;
    int column_capacity;
    int line_number;
    long end_offset; // Added: Byte offset after CREATE TABLE definition
} TableInfo;

// Structure to hold one index entry
typedef struct {
    char *type; // e.g., "TABLE", "INDEX", "FUNCTION", "PROCEDURE"
    char *name;
    int line_number;
    
    // For TABLE entries only
    TableInfo *table_info; // Will be NULL for non-table entries
} IndexEntry;

typedef struct {
    IndexEntry *entries;
    int count;
    int capacity;
} SqlIndex;

typedef struct {
    FILE *file;                 // Renamed from fp
    char *buffer;
    size_t buffer_size;         // Renamed from buffer_alloc_size
    size_t buffer_data_len;     // Added
    size_t global_offset;       // Added
    int current_line;           // Added
    long last_newline_offset;   // Added
    ParserState state;          // Added
    SqlIndex index;
    bool error_occurred; // Flag to indicate if an error stopped processing
} ParsingContext;

// --- Function Declarations ---

// Initialize the parsing context (opens file, allocates buffer)
bool initialize_context(ParsingContext *ctx, const char *filename);

// Free resources held by the context (closes file, frees memory)
void cleanup_context(ParsingContext *ctx);

// Main loop for reading and processing the file
bool process_sql_file(ParsingContext *ctx);

// Print the indexed results
void print_results(const SqlIndex *index);
void cleanup_index(SqlIndex *index); // Function to clean up only the index structure
bool read_index_from_file(SqlIndex *index, const char *index_filename); // Function to read index from file
bool write_index_to_file(const SqlIndex *index, const char *index_filename); // Function to write index to file

// Function to display interactive table selection and column display
void display_table_columns_ui(SqlIndex *index);

// Function to extract column information from CREATE TABLE statement
bool parse_table_columns(ParsingContext *ctx, TableInfo *table_info, const char *start_ptr, const char *end_ptr);

// Function to get a sample of the first data row from an INSERT statement
char* get_first_row_sample(const char *filename, long start_offset, const char *table_name);

#endif // SQL_INDEXER_H
