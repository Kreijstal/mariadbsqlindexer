#include "sql_indexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // For isspace, isalnum
#include <stdbool.h>
#include <errno.h> // Include for errno
#include <strings.h> // Include for strncasecmp
#include <cjson/cJSON.h>

#if defined(_WIN32) || defined(_WIN64) || defined(__MSYS__)
// Windows/MSYS2 doesn't have strcasestr, so we provide one.
// This is a simple implementation using strncasecmp.
char *strcasestr(const char *haystack, const char *needle) {
    if (!needle || *needle == '\0') {
        return (char *)haystack;
    }
    size_t needle_len = strlen(needle);
    for (; *haystack; ++haystack) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
    }
    return NULL;
}
#endif

// --- Constants ---
const char *CREATE_TABLE_KEYWORD = "CREATE TABLE";
const size_t CREATE_TABLE_LEN = 12; // strlen("CREATE TABLE")
const size_t CHUNK_SIZE = 4096; // Read file in 4KB chunks
const size_t BUFFER_EXTRA_MARGIN = 256; // Extra space for potential overflows

// --- Static Helper Function Declarations ---
static bool ensure_buffer_capacity(ParsingContext *ctx, size_t required_size);
static bool add_index_entry(SqlIndex *index, const char *type, const char *name, int line_number);
static bool add_table_entry(SqlIndex *index, const char *name, int line_number);
// Updated signature for process_chunk
static size_t process_chunk(ParsingContext *ctx);
static const char* find_next_token(const char *ptr, const char *end);
static size_t get_token_length(const char *token_start);
static bool add_column_to_table(TableInfo *table_info, const char *name, const char *type, bool is_primary_key, 
                               bool is_not_null, bool is_auto_increment, const char *default_value);
static const char* find_table_body_start(const char *ptr, const char *end);
static const char* find_table_body_end(const char *ptr, const char *end);
static void cleanup_table_info(TableInfo *table_info);
static bool calculate_sha256(const char *filename, char *hash_buffer);

// --- SHA256 Calculation ---
// Calculates the SHA256 hash of a file using the `sha256sum` command.
// Returns true on success and populates the `hash_buffer` (must be 65 bytes).
// Returns false on failure.
static bool calculate_sha256(const char *filename, char *hash_buffer) {
    char command[1024];
    snprintf(command, sizeof(command), "sha256sum %s", filename);

    FILE *pipe = popen(command, "r");
    if (!pipe) {
        perror("popen() failed");
        return false;
    }

    if (fread(hash_buffer, 1, 64, pipe) != 64) {
        fprintf(stderr, "Error reading SHA256 from pipe.\n");
        pclose(pipe);
        return false;
    }
    hash_buffer[64] = '\0'; // Null-terminate the hash string

    pclose(pipe);
    return true;
}
// --- Function Implementations ---

bool initialize_context(ParsingContext *ctx, const char *filename) {
    ctx->file = fopen(filename, "rb"); // Use file instead of fp
    if (!ctx->file) { // Use file instead of fp
        fprintf(stderr, "Error opening file '%s': %s\n", filename, strerror(errno));
        return false;
    }

    ctx->buffer_size = CHUNK_SIZE + BUFFER_EXTRA_MARGIN; // Use buffer_size
    ctx->buffer = malloc(ctx->buffer_size); // Use buffer_size
    if (!ctx->buffer) {
        perror("Failed to allocate read buffer");
        // ctx->file is open, cleanup will handle it
        return false;
    }

    // Initialize other context members
    ctx->buffer_data_len = 0;
    ctx->global_offset = 0;
    ctx->current_line = 1;
    ctx->last_newline_offset = -1; // Start before the file begins
    ctx->state = STATE_CODE;
    ctx->index = (SqlIndex){"", NULL, 0, 0}; // Use SqlIndex, correct initialization
    ctx->error_occurred = false;

    return true;
}

void cleanup_context(ParsingContext *ctx) {
    if (ctx->file) { // Use file instead of fp
        fclose(ctx->file); // Use file instead of fp
        ctx->file = NULL; // Use file instead of fp
    }
    free(ctx->buffer);
    ctx->buffer = NULL;

    // Free index data
    cleanup_index(&ctx->index);
}

// Separate cleanup for index structure (used when loading from file)
void cleanup_index(SqlIndex *index) {
    if (index && index->entries) {
        for (int i = 0; i < index->count; ++i) {
            free(index->entries[i].type);
            free(index->entries[i].name);
            
            // Clean up table info if this is a table entry
            if (index->entries[i].table_info != NULL) {
                cleanup_table_info(index->entries[i].table_info);
                free(index->entries[i].table_info);
                index->entries[i].table_info = NULL;
            }
        }
        free(index->entries);
        index->entries = NULL;
        index->count = 0;
        index->capacity = 0;
    }
}

static void cleanup_table_info(TableInfo *table_info) {
    if (table_info) {
        free(table_info->name);
        
        if (table_info->columns) {
            for (int i = 0; i < table_info->column_count; i++) {
                free(table_info->columns[i].name);
                free(table_info->columns[i].type);
                free(table_info->columns[i].default_value); // May be NULL, free handles NULL
            }
            free(table_info->columns);
        }
    }
}

bool process_sql_file(ParsingContext *ctx) {
    size_t bytes_read;

    while (true) {
        // Ensure buffer has space for next chunk
        if (!ensure_buffer_capacity(ctx, CHUNK_SIZE)) {
             ctx->error_occurred = true; // Ensure error is flagged
             return false;
        }

        // Read next chunk
        bytes_read = fread(ctx->buffer + ctx->buffer_data_len, 1, CHUNK_SIZE, ctx->file); // Use file, buffer_data_len

        if (bytes_read == 0) {
            if (ferror(ctx->file)) { // Use file
                perror("Error reading file");
                ctx->error_occurred = true;
            }
            // Break if EOF or error, but process any remaining data first
            if (ctx->buffer_data_len == 0) break; // Nothing left to process
             // else proceed to process the final partial buffer
        }

        ctx->buffer_data_len += bytes_read; // Use buffer_data_len

        // Process the data currently in the buffer
        size_t processed_len = process_chunk(ctx); // Updated call

        if (ctx->error_occurred) {
            return false; // Stop processing on fatal error (e.g., alloc failure)
        }

        // Update global offset based on processed data
        ctx->global_offset += processed_len; // Use global_offset

        // Shift remaining unprocessed data to the beginning
        if (processed_len < ctx->buffer_data_len) { // Use buffer_data_len
            size_t remaining_len = ctx->buffer_data_len - processed_len; // Use buffer_data_len
            memmove(ctx->buffer, ctx->buffer + processed_len, remaining_len);
            ctx->buffer_data_len = remaining_len; // Use buffer_data_len
        } else {
            ctx->buffer_data_len = 0; // All data processed // Use buffer_data_len
        }

        // If we hit EOF in the read above and processed everything, we're done.
        if (bytes_read == 0 && ctx->buffer_data_len == 0) { // Use buffer_data_len
            break;
        }
    }
    // Process any final remaining data in the buffer if EOF was reached before processing
    if (ctx->buffer_data_len > 0 && !ctx->error_occurred) {
        process_chunk(ctx);
        ctx->buffer_data_len = 0; // Mark as processed
    }

    return !ctx->error_occurred;
}

// Print the indexed results
void print_results(const SqlIndex *index) {
    printf("Indexed Objects:\n");
    // Updated header to reflect actual IndexEntry fields
    printf("%-10s %-10s %s\n", "Line", "Type", "Name");
    printf("--------------------------------------------------\n");
    if (index->count > 0) {
        for (int i = 0; i < index->count; ++i) {
            // Use line_number, type, name from IndexEntry
            printf("%-10d %-10s %s\n",
                   index->entries[i].line_number,
                   index->entries[i].type,
                   index->entries[i].name);
                   
            // Print columns if this is a table and has column information
            if (strcmp(index->entries[i].type, "TABLE") == 0 && 
                index->entries[i].table_info != NULL &&
                index->entries[i].table_info->column_count > 0) {
                
                printf("   Columns:\n");
                for (int j = 0; j < index->entries[i].table_info->column_count; j++) {
                    ColumnInfo *col = &index->entries[i].table_info->columns[j];
                    printf("     %-20s %-15s", col->name, col->type);
                    
                    // Print column attributes
                    if (col->is_primary_key) printf(" PK");
                    if (col->is_not_null) printf(" NOT NULL");
                    if (col->is_auto_increment) printf(" AUTO_INCREMENT");
                    if (col->default_value) printf(" DEFAULT %s", col->default_value);
                    
                    printf("\n");
                }
                printf("\n");
            }
        }
    } else {
        printf("No indexable objects found or index is empty.\n");
    }
}

// --- Index File I/O ---

// Format: TYPE,NAME,LINE\n
// Helper to read a line, handling potential buffer overflows
static char* read_line_from_index(FILE *fp, char *buffer, size_t buffer_size) {
    if (fgets(buffer, buffer_size, fp) == NULL) {
        return NULL; // EOF or error
    }
    // Check if the whole line was read
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] != '\n') {
        // Line was too long for the buffer or EOF without newline
        if (!feof(fp)) {
            fprintf(stderr, "Warning: Line in index file exceeds buffer size (%zu)\n", buffer_size);
            // Consume rest of the line
            int ch;
            while ((ch = fgetc(fp)) != '\n' && ch != EOF);
            // Mark buffer as potentially incomplete by ensuring null termination
            // (though fgets should already do this)
            buffer[buffer_size - 1] = '\0';
        } // else: EOF reached after partial line read, which is okay
    } else if (len > 0) {
        // Remove trailing newline
        buffer[len - 1] = '\0';
    }
    return buffer;
}

bool read_index_from_file(SqlIndex *index, const char *index_filename) {
    FILE *fp = fopen(index_filename, "r");
    if (!fp) {
        perror("Error opening index file for reading");
        return false;
    }

    // Initialize index structure
    index->entries = NULL;
    index->count = 0;
    index->capacity = 0;
    memset(index->sql_file_sha256, 0, sizeof(index->sql_file_sha256));

    char line_buffer[1024]; // Buffer for reading lines

    // Read the SHA256 hash from the first line
    if (read_line_from_index(fp, line_buffer, sizeof(line_buffer))) {
        if (strncmp(line_buffer, "SHA256:", 7) == 0) {
            strncpy(index->sql_file_sha256, line_buffer + 7, 64);
            index->sql_file_sha256[64] = '\0';
        } else {
            fprintf(stderr, "Warning: Index file does not contain SHA256 hash.\n");
            // Rewind to process the line as a regular entry
            fseek(fp, 0, SEEK_SET);
        }
    }
    char type_buffer[256];
    char name_buffer[512];
    int line_number;
    int read_count = 0;

    while (read_line_from_index(fp, line_buffer, sizeof(line_buffer))) {
        // Check for COLUMN lines first
        if (strncmp(line_buffer, "COLUMN,", 7) == 0) {
            // Parse column info: COLUMN,TABLE_NAME,COLUMN_NAME,TYPE,IS_PK,IS_NOT_NULL,IS_AUTO_INC,DEFAULT
            char table_name[256], col_name[256], col_type[256], default_val[256];
            bool is_pk, is_nn, is_ai;
            int is_pk_int, is_nn_int, is_ai_int;
            int vals_read = sscanf(line_buffer, "COLUMN,%255[^,],%255[^,],%255[^,],%d,%d,%d,%255[^\n]", 
                                table_name, col_name, col_type, &is_pk_int, &is_nn_int, &is_ai_int, default_val);
            is_pk = is_pk_int;
            is_nn = is_nn_int;
            is_ai = is_ai_int;
            
            // Ensure we read at least the table, column name, and type
            if (vals_read >= 3 && strcmp(table_name, name_buffer) == 0) {
                // Find the table entry we just added
                IndexEntry *entry = &index->entries[index->count - 1];
                if (entry->table_info == NULL) {
                    // Should never happen if add_table_entry worked properly
                    fprintf(stderr, "Error: Table entry has no table_info structure.\n");
                    fclose(fp);
                    cleanup_index(index);
                    return false;
                }
                
                // Add the column
                if (!add_column_to_table(entry->table_info, col_name, col_type, 
                                      is_pk, is_nn, is_ai, 
                                      (vals_read >= 7 && default_val[0] != '\0') ? default_val : NULL)) {
                    fprintf(stderr, "Error adding column info for %s.%s\n", name_buffer, col_name);
                    fclose(fp);
                    cleanup_index(index);
                    return false;
                }
            } else {
                // Malformed column line or different table
                fprintf(stderr, "Warning: Malformed column entry in index file: %s\n", line_buffer);
            }
            continue; // Move to the next line
        }

        // Parse main entry: TYPE,NAME,LINE[,END_OFFSET]
        long end_offset = -1;
        read_count = sscanf(line_buffer, "%255[^,],%511[^,],%d,%ld", type_buffer, name_buffer, &line_number, &end_offset);

        if (read_count >= 3) { // Need at least TYPE, NAME, LINE
            if (strcmp(type_buffer, "TABLE") == 0) {
                if (!add_table_entry(index, name_buffer, line_number)) {
                    fprintf(stderr, "Error adding table entry while reading index file.\n");
                    fclose(fp);
                    cleanup_index(index);
                    return false;
                }
                // If end_offset was read (read_count == 4), store it
                if (read_count == 4 && index->count > 0) {
                    index->entries[index->count - 1].table_info->end_offset = end_offset;
                }
            } else {
                // Non-table entry
                if (!add_index_entry(index, type_buffer, name_buffer, line_number)) {
                    fprintf(stderr, "Error adding entry while reading index file.\n");
                    fclose(fp);
                    cleanup_index(index); // Clean up partially read index
                    return false;
                }
            }
        } else if (strlen(line_buffer) > 0) { // Avoid warning on empty lines
             fprintf(stderr, "Warning: Malformed line in index file: %s\n", line_buffer);
             // Decide if you want to stop or continue on malformed lines
             // continue;
             // fclose(fp);
             // cleanup_index(index);
             // return false; // Uncomment to be strict
        }
    }

    if (ferror(fp)) {
        perror("Error reading from index file");
        fclose(fp);
        cleanup_index(index);
        return false;
    }

    fclose(fp);
    printf("Successfully loaded %d entries from index file '%s'.\n", index->count, index_filename);
    return true;
}

bool write_index_to_file(const SqlIndex *index, const char *index_filename, const char *sql_file_sha256) {
    FILE *fp = fopen(index_filename, "w");
    if (!fp) {
        perror("Error opening index file for writing");
        return false;
    }

    // Write SHA256 hash if provided
    if (sql_file_sha256) {
        if (fprintf(fp, "SHA256:%s\n", sql_file_sha256) < 0) {
            perror("Error writing SHA256 to index file");
            fclose(fp);
            return false;
        }
    }

    for (int i = 0; i < index->count; ++i) {
        // Write main entry
        if (strcmp(index->entries[i].type, "TABLE") == 0 && index->entries[i].table_info) {
            // Table entry: include end_offset
            if (fprintf(fp, "%s,%s,%d,%ld\n", index->entries[i].type, index->entries[i].name, index->entries[i].line_number, index->entries[i].table_info->end_offset) < 0) {
                perror("Error writing to index file");
                fclose(fp);
                // Optionally remove the partially written file
                // remove(index_filename);
                return false;
            }
            // Write column information for this table
            TableInfo *table = index->entries[i].table_info;
            for (int j = 0; j < table->column_count; j++) {
                ColumnInfo *col = &table->columns[j];
                // Write column entry: COLUMN,TABLE_NAME,COLUMN_NAME,TYPE,IS_PK,IS_NOT_NULL,IS_AUTO_INC,DEFAULT
                if (fprintf(fp, "COLUMN,%s,%s,%s,%d,%d,%d,%s\n", 
                           table->name, col->name, col->type,
                           col->is_primary_key ? 1 : 0, 
                           col->is_not_null ? 1 : 0,
                           col->is_auto_increment ? 1 : 0,
                           col->default_value ? col->default_value : "") < 0) {
                    perror("Error writing column info to index file");
                    fclose(fp);
                    return false;
                }
            }
        } else {
            // Non-table entry: TYPE,NAME,LINE
            if (fprintf(fp, "%s,%s,%d\n", index->entries[i].type, index->entries[i].name, index->entries[i].line_number) < 0) {
                perror("Error writing to index file");
                fclose(fp);
                // Optionally remove the partially written file
                // remove(index_filename);
                return false;
            }
        }
    }

    if (fclose(fp) != 0) {
        perror("Error closing index file after writing");
        return false;
    }

    return true;
}

// --- Static Helper Function Implementations ---

static bool ensure_buffer_capacity(ParsingContext *ctx, size_t required_size) {
    // Use buffer_size and buffer_data_len
    if (ctx->buffer_size - ctx->buffer_data_len < required_size) {
        // Not enough space, realloc buffer
        size_t new_size = ctx->buffer_size * 2;
        while (new_size - ctx->buffer_data_len < required_size) {
            new_size *= 2;
            if (new_size < ctx->buffer_size) { // Check for overflow
                 fprintf(stderr, "Error: Buffer size overflow during reallocation\n");
                 ctx->error_occurred = true;
                 return false;
            }
        }
        char *new_buffer = realloc(ctx->buffer, new_size);
        if (!new_buffer) {
            perror("Failed to reallocate buffer");
            ctx->error_occurred = true;
            return false;
        }
        ctx->buffer = new_buffer;
        ctx->buffer_size = new_size; // Use buffer_size
    }
    return true;
}

static bool add_index_entry(SqlIndex *index, const char *type, const char *name, int line_number) {
    if (index->count >= index->capacity) {
        size_t new_capacity = index->capacity == 0 ? 16 : index->capacity * 2;
        if (new_capacity < index->capacity) {
             fprintf(stderr, "Error: Index capacity overflow\n");
             return false;
        }
        // Use IndexEntry instead of SqlEntry
        IndexEntry *new_entries = realloc(index->entries, new_capacity * sizeof(IndexEntry));
        if (!new_entries) {
            perror("Failed to reallocate memory for index entries");
            return false;
        }
        index->entries = new_entries;
        index->capacity = new_capacity;
    }

    // Check for allocation failures for strdup
    char *type_copy = strdup(type);
    char *name_copy = strdup(name);
    if (!type_copy || !name_copy) {
        perror("Failed to duplicate string for index entry");
        free(type_copy); // free if one succeeded but the other failed
        free(name_copy);
        return false;
    }

    index->entries[index->count].type = type_copy;
    index->entries[index->count].name = name_copy;
    index->entries[index->count].line_number = line_number;
    index->entries[index->count].table_info = NULL; // Initialize table_info to NULL
    index->count++;
    return true;
}

static bool add_table_entry(SqlIndex *index, const char *name, int line_number) {
    // First, check if this table already exists in the index.
    // This can happen if a CREATE TABLE statement spans multiple chunks.
    for (int i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].type, "TABLE") == 0 && strcmp(index->entries[i].name, name) == 0) {
            // Table already exists, no need to add it again.
            // We can assume we're now processing the complete statement.
            return true;
        }
    }

    // If it doesn't exist, add it.
    if (index->count >= index->capacity) {
        size_t new_capacity = index->capacity == 0 ? 16 : index->capacity * 2;
        IndexEntry *new_entries = realloc(index->entries, new_capacity * sizeof(IndexEntry));
        if (!new_entries) {
            perror("Failed to reallocate memory for index entries");
            return false;
        }
        index->entries = new_entries;
        index->capacity = new_capacity;
    }

    // Check for allocation failures for strdup
    char *type_copy = strdup("TABLE");
    char *name_copy = strdup(name);
    if (!type_copy || !name_copy) {
        perror("Failed to duplicate string for table entry");
        free(type_copy);
        free(name_copy);
        return false;
    }

    // Create and initialize the TableInfo structure
    TableInfo *table_info = (TableInfo *)calloc(1, sizeof(TableInfo));
    if (!table_info) {
        perror("Failed to allocate memory for table info");
        free(type_copy);
        free(name_copy);
        return false;
    }
    
    table_info->name = strdup(name);
    if (!table_info->name) {
        perror("Failed to duplicate table name for table info");
        free(type_copy);
        free(name_copy);
        free(table_info);
        return false;
    }
    
    table_info->columns = NULL;
    table_info->column_count = 0;
    table_info->column_capacity = 0;
    table_info->line_number = line_number;
    table_info->end_offset = -1; // Initialize end_offset
    
    // Now populate the entry
    index->entries[index->count].type = type_copy;
    index->entries[index->count].name = name_copy;
    index->entries[index->count].line_number = line_number;
    index->entries[index->count].table_info = table_info;
    index->count++;
    
    return true;
}

static bool add_column_to_table(TableInfo *table_info, const char *name, const char *type, 
                              bool is_primary_key, bool is_not_null, bool is_auto_increment, 
                              const char *default_value) {
    if (!table_info) return false;
    
    // Ensure we have capacity
    if (table_info->column_count >= table_info->column_capacity) {
        int new_capacity = table_info->column_capacity == 0 ? 8 : table_info->column_capacity * 2;
        ColumnInfo *new_columns = realloc(table_info->columns, new_capacity * sizeof(ColumnInfo));
        if (!new_columns) {
            perror("Failed to allocate memory for columns");
            return false;
        }
        table_info->columns = new_columns;
        table_info->column_capacity = new_capacity;
    }
    
    // Initialize the column
    ColumnInfo *col = &table_info->columns[table_info->column_count];
    
    col->name = strdup(name);
    col->type = strdup(type);
    if (!col->name || !col->type) {
        perror("Failed to duplicate column name or type");
        free(col->name); // These may be NULL, but that's okay
        free(col->type);
        return false;
    }
    
    col->is_primary_key = is_primary_key;
    col->is_not_null = is_not_null;
    col->is_auto_increment = is_auto_increment;
    col->default_value = default_value ? strdup(default_value) : NULL;
    
    table_info->column_count++;
    return true;
}

// Updated process_chunk implementation to parse table columns
static size_t process_chunk(ParsingContext *ctx) {
    const char *ptr = ctx->buffer;
    const char *end = ctx->buffer + ctx->buffer_data_len;
    const char *chunk_start = ctx->buffer;
    size_t processed_bytes = 0;

    while (ptr < end) {
        // --- State Machine Logic (Simplified Example) ---
        // This needs to be fleshed out to handle comments, strings etc.
        // For now, just look for CREATE TABLE naively.

        // Track line numbers
        if (*ptr == '\n') {
            ctx->current_line++;
            ctx->last_newline_offset = ctx->global_offset + (ptr - chunk_start);
        }

        // Simple check for "CREATE TABLE" (case-insensitive)
        // This is a basic example and doesn't handle comments/strings correctly
        if (ctx->state == STATE_CODE && (end - ptr >= CREATE_TABLE_LEN)) {
            if (strncasecmp(ptr, CREATE_TABLE_KEYWORD, CREATE_TABLE_LEN) == 0) {
                const char* next_char_ptr = ptr + CREATE_TABLE_LEN;
                // Ensure it's followed by whitespace or end of buffer
                if (next_char_ptr == end || isspace((unsigned char)*next_char_ptr)) {
                    const char *token_start = find_next_token(next_char_ptr, end);
                    if (token_start) {
                        size_t token_len = get_token_length(token_start);
                        if (token_len > 0 && token_start + token_len <= end) {
                            char *table_name = malloc(token_len + 1);
                            if (table_name) {
                                strncpy(table_name, token_start, token_len);
                                table_name[token_len] = '\0';
                                // Remove potential backticks
                                if (table_name[0] == '`' && table_name[token_len - 1] == '`') {
                                    memmove(table_name, table_name + 1, token_len - 2);
                                    table_name[token_len - 2] = '\0';
                                }

                                // Add the table entry
                                if (!add_table_entry(&ctx->index, table_name, ctx->current_line)) {
                                    ctx->error_occurred = true;
                                    free(table_name);
                                    return processed_bytes; // Stop processing on error
                                }
                                
                                // Find the start of the table body (look for '(' after table name)
                                const char *table_body_start = find_table_body_start(token_start + token_len, end);
                                if (table_body_start) {
                                    // Find the last entry for this table name
                                    int table_idx = -1;
                                    for (int i = ctx->index.count - 1; i >= 0; i--) {
                                        if (strcmp(ctx->index.entries[i].type, "TABLE") == 0 && strcmp(ctx->index.entries[i].name, table_name) == 0) {
                                            table_idx = i;
                                            break;
                                        }
                                    }
                                    if (table_idx == -1) {
                                        // Should not happen due to add_table_entry logic
                                        ctx->error_occurred = true;
                                        free(table_name);
                                        return processed_bytes;
                                    }
                                    TableInfo *table_info = ctx->index.entries[table_idx].table_info;
                                    
                                    // Find the end of the table definition
                                    const char *table_body_end = find_table_body_end(table_body_start, end);
                                    
                                    // If we can find both the start and end, parse the columns
                                    if (table_body_end) {
                                        // Store the global offset after the CREATE TABLE definition
                                        table_info->end_offset = ctx->global_offset + (table_body_end - chunk_start);

                                        // Parse column definitions
                                        if (!parse_table_columns(ctx, table_info, table_body_start, table_body_end)) {
                                            fprintf(stderr, "Warning: Failed to parse columns for table '%s'\n", table_name);
                                            // Continue processing even if column parsing fails
                                        }
                                    
                                        // Move past the table definition
                                        ptr = table_body_end;
                                    } else {
                                        // The CREATE TABLE statement is split across chunks.
                                        processed_bytes = (ptr - chunk_start);
                                        free(table_name);
                                        return processed_bytes; // Return, so the buffer can be refilled
                                    }
                                } else {
                                    // Move just past the table name
                                    ctx->index.entries[ctx->index.count - 1].table_info->end_offset = ctx->global_offset + (token_start + token_len - chunk_start);
                                    ptr = token_start + token_len;
                                }
                                
                                free(table_name);
                                processed_bytes = (ptr - chunk_start);
                                continue; // Continue loop
                            } else {
                                perror("Failed to allocate memory for table name");
                                ctx->error_occurred = true;
                                return processed_bytes;
                            }
                        }
                    }
                    // If we found CREATE TABLE but couldn't parse name, advance past keyword
                    ptr += CREATE_TABLE_LEN;
                    processed_bytes = (ptr - chunk_start);
                    continue;
                }
            }
        }

        // --- End State Machine Logic ---

        ptr++;
        processed_bytes = (ptr - chunk_start);

        // Need to handle buffer boundaries carefully - don't process the last few bytes
        // if they might be part of a multi-byte sequence or keyword split across chunks.
        // For this simple example, we process up to the end.
    }

    // Return the number of bytes fully processed in this chunk.
    // If the chunk ends mid-token/comment, we might need to return less
    // and keep the remainder for the next read (requires more complex logic).
    return processed_bytes;
}

// Find the start of the table body (the opening parenthesis)
static const char* find_table_body_start(const char *ptr, const char *end) {
    while (ptr < end) {
        if (*ptr == '(') return ptr + 1; // Return position after the opening parenthesis
        ptr++;
    }
    return NULL; // Not found within buffer bounds
}

// Find the end of the table body (the closing parenthesis)
static const char* find_table_body_end(const char *ptr, const char *end) {
    int depth = 1; // We start after the opening parenthesis
    while (ptr < end) {
        if (*ptr == '(') {
            depth++;
        } else if (*ptr == ')') {
            depth--;
            if (depth == 0) return ptr + 1; // Return position after the closing parenthesis
        }
        ptr++;
    }
    return NULL; // Not found within buffer bounds or not balanced
}

// Parse column definitions from a CREATE TABLE statement
bool parse_table_columns(ParsingContext *ctx, TableInfo *table_info, const char *start_ptr, const char *end_ptr) {
    const char *p = start_ptr;
    bool success = true;

    while (p < end_ptr) {
        // Skip leading whitespace and commas from previous iteration
        while (p < end_ptr && (isspace((unsigned char)*p) || *p == ',')) {
            p++;
        }
        if (p >= end_ptr) break;

        const char *line_start = p;
        const char *line_end = NULL;
        int paren_depth = 0;

        // Find the end of the current definition (the next comma at parenthesis depth 0)
        const char *scanner = p;
        while (scanner < end_ptr) {
            if (*scanner == '(') paren_depth++;
            else if (*scanner == ')') paren_depth--;
            else if (*scanner == ',' && paren_depth == 0) {
                line_end = scanner;
                break;
            }
            scanner++;
        }
        if (!line_end) {
            // Could be the last definition before the closing ')' of the table
            line_end = end_ptr -1; // Go to the char before the final ')'
            while(line_end > line_start && isspace((unsigned char)*line_end)) line_end--;
            line_end++; // point after the last char of the definition
        }


        // Copy the definition to a mutable buffer for strtok
        char def[2048];
        size_t len = line_end - line_start;
        if (len >= sizeof(def)) len = sizeof(def) - 1;
        strncpy(def, line_start, len);
        def[len] = '\0';
        
        // Create a copy for checking the first word, as strtok modifies it
        char def_copy[2048];
        strncpy(def_copy, def, sizeof(def_copy));

        char *first_word = strtok(def_copy, " \t\n\r");
        if (!first_word) {
            p = line_end + 1;
            continue;
        }

        if (strcasecmp(first_word, "PRIMARY") == 0 || strcasecmp(first_word, "UNIQUE") == 0) {
            // It's a table-level constraint like PRIMARY KEY (col1, col2)
            char *key_type = first_word;
            char *cols_part = strchr(def, '(');
            if (cols_part) {
                char *cols_end = strrchr(cols_part, ')');
                if (cols_end) *cols_end = '\0';
                
                char *col_name = strtok(cols_part + 1, ",` ");
                while (col_name) {
                    if (strlen(col_name) > 0) {
                        for (int i = 0; i < table_info->column_count; i++) {
                            if (strcmp(table_info->columns[i].name, col_name) == 0) {
                                if (strcasecmp(key_type, "PRIMARY") == 0) {
                                    table_info->columns[i].is_primary_key = true;
                                }
                                // Could handle UNIQUE here as well
                                break;
                            }
                        }
                    }
                    col_name = strtok(NULL, ",` ");
                }
            }
        } else if (strcasecmp(first_word, "CONSTRAINT") != 0 && strcasecmp(first_word, "KEY") != 0 && strcasecmp(first_word, "FOREIGN") != 0) {
            // It's a column definition
            char *col_name = NULL, *col_type = NULL, *default_value = NULL;
            bool is_pk = false, is_nn = false, is_ai = false;

            // Use the original `def` buffer for parsing the column
            char *token = strtok(def, " \t\n\r");
            if (token) {
                // First token is the name
                if (token[0] == '`') {
                    col_name = token + 1;
                    char *end_tick = strchr(col_name, '`');
                    if (end_tick) *end_tick = '\0';
                } else {
                    col_name = token;
                }

                // Second token is the type
                token = strtok(NULL, " \t\n\r");
                if (token) {
                    // The type might contain spaces or parentheses, e.g., "VARCHAR(255)", "ENUM('M', 'F')"
                    // We need to reconstruct it carefully.
                    char type_buffer[256];
                    strncpy(type_buffer, token, sizeof(type_buffer) - 1);
                    type_buffer[sizeof(type_buffer) - 1] = '\0';

                    int paren_depth = 0;
                    for (char *c = type_buffer; *c; c++) {
                        if (*c == '(') paren_depth++;
                        else if (*c == ')') paren_depth--;
                    }

                    while (paren_depth > 0 && (token = strtok(NULL, " \t\n\r,"))) {
                        strncat(type_buffer, " ", sizeof(type_buffer) - strlen(type_buffer) - 1);
                        strncat(type_buffer, token, sizeof(type_buffer) - strlen(type_buffer) - 1);
                        for (char *c = (char*)token; *c; c++) {
                            if (*c == '(') paren_depth++;
                            else if (*c == ')') paren_depth--;
                        }
                    }
                    col_type = type_buffer;
                }


                // Parse remaining attributes
                while ((token = strtok(NULL, " \t\n\r,"))) {
                    if (strcasecmp(token, "NOT") == 0) {
                        char* next = strtok(NULL, " \t\n\r,");
                        if (next && strcasecmp(next, "NULL") == 0) is_nn = true;
                    } else if (strcasecmp(token, "AUTO_INCREMENT") == 0) {
                        is_ai = true;
                    } else if (strcasecmp(token, "PRIMARY") == 0) {
                         char* next = strtok(NULL, " \t\n\r,");
                        if (next && strcasecmp(next, "KEY") == 0) is_pk = true;
                    } else if (strcasecmp(token, "DEFAULT") == 0) {
                        default_value = strtok(NULL, " \t\n\r,");
                    }
                }
            }
            
            if (col_name && col_type) {
                if (!add_column_to_table(table_info, col_name, col_type, is_pk, is_nn, is_ai, default_value)) {
                    fprintf(stderr, "Error adding column %s to table %s\n", col_name, table_info->name);
                    success = false;
                }
            }
        }
        
        p = line_end + 1;
    }
    return success;
}

static const char* find_next_token(const char *ptr, const char *end) {
    // Skip whitespace
    while (ptr < end && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    return ptr < end ? ptr : NULL;
}

static size_t get_token_length(const char *token_start) {
    const char *ptr = token_start;
    while (*ptr && !isspace((unsigned char)*ptr) && *ptr != ',' && *ptr != '(') {
        ptr++;
    }
    return ptr - token_start;
}

// --- New Function: Get First Row Sample ---

// Reads from the SQL file starting at a given offset to find the first row
// of an INSERT statement for the specified table.
// Returns a dynamically allocated string with the sample (up to 300 chars or "BLOB"),
// or NULL on error or if not found.
// Caller must free the returned string.
char* get_first_row_sample(const char *filename, long start_offset, const char *table_name) {
    if (start_offset < 0 || !filename || !table_name) {
        return NULL;
    }

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("get_first_row_sample: Error opening file");
        return NULL;
    }

    if (fseek(fp, start_offset, SEEK_SET) != 0) {
        perror("get_first_row_sample: Error seeking in file");
        fclose(fp);
        return NULL;
    }

    char buffer[CHUNK_SIZE + 1]; // Read buffer
    char insert_pattern[512];
    // Create two common patterns (with and without backticks)
    snprintf(insert_pattern, sizeof(insert_pattern), "INSERT INTO `%s` VALUES (", table_name);
    char insert_pattern_no_ticks[512];
    snprintf(insert_pattern_no_ticks, sizeof(insert_pattern), "INSERT INTO %s VALUES (", table_name);

    size_t pattern_len = strlen(insert_pattern);
    size_t pattern_no_ticks_len = strlen(insert_pattern_no_ticks);
    size_t bytes_read;
    char *found_pattern = NULL;
    size_t found_pattern_len = 0;
    long current_offset = start_offset;
    char *sample = NULL;

    // Read chunks until INSERT is found or EOF
    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, fp)) > 0) {
        buffer[bytes_read] = '\0'; // Null-terminate the buffer

        // Search for the INSERT patterns (case-insensitive for keywords)
        char *search_start = buffer;
        while (search_start < buffer + bytes_read) {
            // Skip leading whitespace/comments (basic skip)
            while (search_start < buffer + bytes_read && (isspace(*search_start) || *search_start == '-' || *search_start == '/')) {
                 // Basic comment skip - might need improvement
                 if (*search_start == '-' && search_start + 1 < buffer + bytes_read && *(search_start+1) == '-') {
                     char *eol = strchr(search_start, '\n');
                     if (eol) search_start = eol + 1; else search_start = buffer + bytes_read;
                 } else if (*search_start == '/' && search_start + 1 < buffer + bytes_read && *(search_start+1) == '*') {
                     char *end_comment = strstr(search_start + 2, "*/");
                     if (end_comment) search_start = end_comment + 2; else search_start = buffer + bytes_read; // Assume comment extends beyond buffer
                 } else {
                     search_start++;
                 }
            }
            if (search_start >= buffer + bytes_read) break;

            // Check for patterns
            if (strncasecmp(search_start, "INSERT INTO", 11) == 0) {
                 // Found potential start, now check table name and VALUES
                 const char* after_insert = search_start + 11;
                 while (isspace(*after_insert)) after_insert++;

                 bool match = false;
                 if (*after_insert == '`' && strncmp(after_insert + 1, table_name, strlen(table_name)) == 0 && *(after_insert + 1 + strlen(table_name)) == '`') {
                     // Match with backticks
                     const char* after_table = after_insert + 1 + strlen(table_name) + 1;
                     while (isspace(*after_table)) after_table++;
                     if (strncasecmp(after_table, "VALUES", 6) == 0) {
                         const char* after_values = after_table + 6;
                         while (isspace(*after_values)) after_values++;
                         if (*after_values == '(') {
                             found_pattern = (char*)after_values + 1; // Start of row data
                             match = true;
                         }
                     }
                 } else if (strncmp(after_insert, table_name, strlen(table_name)) == 0) {
                     // Match without backticks
                     const char* after_table = after_insert + strlen(table_name);
                     while (isspace(*after_table)) after_table++;
                     if (strncasecmp(after_table, "VALUES", 6) == 0) {
                         const char* after_values = after_table + 6;
                         while (isspace(*after_values)) after_values++;
                         if (*after_values == '(') {
                             found_pattern = (char*)after_values + 1; // Start of row data
                             match = true;
                         }
                     }
                 }

                 if (match) break; // Found the correct INSERT
            }
            // Move to next potential start (e.g., next line or just next char)
            char *next_line = strchr(search_start, '\n');
            search_start = next_line ? next_line + 1 : buffer + bytes_read;
        }

        if (found_pattern) break; // Exit outer loop if found

        current_offset += bytes_read;
        // Need logic to handle patterns split across buffer boundaries (more complex)
    }

    if (found_pattern) {
        // Found the start of the first row data
        const char *row_start = found_pattern;
        const char *row_end = NULL;
        int paren_depth = 1; // Already inside the first opening parenthesis
        bool in_string = false;
        char string_quote = 0;

        // Find the closing parenthesis for *this* row, respecting strings
        const char *p = row_start;
        const char *buffer_end = buffer + bytes_read; // End of current buffer
        while (p < buffer_end) {
            if (!in_string && (*p == '\'' || *p == '"' || *p == '`') && (p == row_start || *(p-1) != '\\')) {
                in_string = true;
                string_quote = *p;
            } else if (in_string && *p == string_quote && (p == row_start || *(p-1) != '\\')) {
                in_string = false;
            }
            else if (!in_string) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') {
                    paren_depth--;
                    if (paren_depth == 0) {
                        row_end = p;
                        break;
                    }
                }
            }
            p++;
        }
        // TODO: Handle rows split across buffer boundaries

        if (row_end) {
            size_t row_len = row_end - row_start;
            // Check for _binary prefix (case-insensitive? Assuming case-sensitive here)
            if (strncmp(row_start, "_binary ", 8) == 0) {
                 sample = strdup("BLOB");
            } else {
                size_t sample_len = row_len < 300 ? row_len : 300;
                sample = malloc(sample_len + 1);
                if (sample) {
                    strncpy(sample, row_start, sample_len);
                    sample[sample_len] = '\0';
                } else {
                    perror("get_first_row_sample: Failed to allocate memory for sample");
                }
            }
        }
    }

    fclose(fp);
    return sample;
}

void dump_table_as_json(const SqlIndex *index, const char *table_name, const char *sql_filename) {
    // Find the table in the index
    TableInfo *table_info = NULL;
    for (int i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].type, "TABLE") == 0 && strcmp(index->entries[i].name, table_name) == 0) {
            table_info = index->entries[i].table_info;
            break;
        }
    }

    if (!table_info) {
        fprintf(stderr, "Table '%s' not found in index.\n", table_name);
        return;
    }

    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    cJSON *table = cJSON_AddObjectToObject(root, table_name);
    cJSON *columns = cJSON_AddArrayToObject(table, "columns");
    for (int i = 0; i < table_info->column_count; ++i) {
        ColumnInfo *col_info = &table_info->columns[i];
        cJSON *column = cJSON_CreateObject();
        cJSON_AddStringToObject(column, "name", col_info->name);
        cJSON_AddStringToObject(column, "type", col_info->type);
        cJSON_AddBoolToObject(column, "is_primary_key", col_info->is_primary_key);
        cJSON_AddBoolToObject(column, "is_not_null", col_info->is_not_null);
        cJSON_AddBoolToObject(column, "is_auto_increment", col_info->is_auto_increment);
        if (col_info->default_value) {
            cJSON_AddStringToObject(column, "default", col_info->default_value);
        }
        cJSON_AddItemToArray(columns, column);
    }

    cJSON *rows = cJSON_AddArrayToObject(table, "rows");

    FILE *fp = fopen(sql_filename, "rb");
    if (!fp) {
        perror("dump_table_as_json: Error opening file");
        cJSON_Delete(root);
        return;
    }

    fseek(fp, table_info->end_offset, SEEK_SET);

    char *buffer = malloc(CHUNK_SIZE + 1);
    if (!buffer) {
        perror("Failed to allocate buffer for JSON dump");
        fclose(fp);
        cJSON_Delete(root);
        return;
    }

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, fp)) > 0) {
        buffer[bytes_read] = '\0';
        char *p = buffer;
        while ((p = strcasestr(p, "INSERT INTO"))) {
            // Basic check for correct table name
            char *name_start = p + 11;
            while (isspace(*name_start) || *name_start == '`') name_start++;
            if (strncmp(name_start, table_name, strlen(table_name)) != 0) {
                p++;
                continue;
            }

            char *values = strcasestr(p, "VALUES");
            if (!values) break;

            p = values + 6;
            // Find the end of the INSERT statement
            char *stmt_end = strchr(p, ';');
            if (!stmt_end) {
                // If no semicolon, assume it ends at the end of the buffer
                // This is a simplification.
                stmt_end = p + strlen(p);
            }

            // Loop through all (...) value sets within this single INSERT statement
            while ((p = strchr(p, '(')) && p < stmt_end) {
                p++; // Move past '('
                cJSON *row_array = cJSON_CreateArray();
                char *row_end = strchr(p, ')');
                if (!row_end || row_end > stmt_end) break; // Row is malformed or part of another statement

                char *value_start = p;
                while (value_start < row_end) {
                    // Find the end of the current value
                    char *value_end = value_start;
                    bool in_string = false;
                    while (value_end < row_end) {
                        if (*value_end == '\'') in_string = !in_string;
                        if (*value_end == ',' && !in_string) break;
                        value_end++;
                    }

                    // Extract and clean the value
                    char value_buffer[256];
                    size_t len = value_end - value_start;
                    if (len >= sizeof(value_buffer)) len = sizeof(value_buffer) - 1;
                    strncpy(value_buffer, value_start, len);
                    value_buffer[len] = '\0';

                    // Trim quotes and whitespace
                    char *trimmed_start = value_buffer;
                    while (isspace(*trimmed_start) || *trimmed_start == '\'') trimmed_start++;
                    char *trimmed_end = trimmed_start + strlen(trimmed_start) - 1;
                    while (trimmed_end > trimmed_start && (isspace(*trimmed_end) || *trimmed_end == '\'')) *trimmed_end-- = '\0';

                    // Add to JSON array
                    char *endptr;
                    double num = strtod(trimmed_start, &endptr);
                    if (*endptr == '\0' && strlen(trimmed_start) > 0) {
                        cJSON_AddItemToArray(row_array, cJSON_CreateNumber(num));
                    } else {
                        cJSON_AddItemToArray(row_array, cJSON_CreateString(trimmed_start));
                    }

                    value_start = value_end + 1;
                }
                cJSON_AddItemToArray(rows, row_array);
                p = row_end + 1;
            }
            p = stmt_end + 1; // Continue searching after this statement
        }
    }

    free(buffer);
    fclose(fp);

    char *json_string = cJSON_Print(root);
    printf("%s\n", json_string);

    cJSON_Delete(root);
    free(json_string);
}
