#include "sql_indexer.h" // Include the new header
#include <string.h>
#include <ctype.h>   // For tolower, isspace, isalnum
#include <errno.h>   // For errno

// --- Constants (Definitions moved to header, declared as extern) ---
const char *CREATE_TABLE_KEYWORD = "CREATE TABLE";
const size_t CREATE_TABLE_LEN = 12; // strlen("CREATE TABLE")
const size_t CHUNK_SIZE = 65536; // 64KB read chunk
const size_t BUFFER_EXTRA_MARGIN = 512; // Safety margin for lookaheads, names etc.




// --- Forward Declarations (Moved to header or internal static) ---
// Internal helper functions not needed outside this file
static size_t process_chunk(ParsingContext *ctx);
static void update_parser_state(ParsingContext *ctx, char current_char, char next_char, char **ptr_ref);
static bool handle_code_state(ParsingContext *ctx, char **ptr_ref, char *buffer_end);
static bool parse_table_name(const char *start_ptr, const char *limit, const char **name_start_out, size_t *name_len_out, const char **end_ptr_out);
static bool add_entry(TableIndex *index, long long offset, long long line, long long col, const char *name_start, size_t name_len);
static int strncasecmp_custom(const char *s1, const char *s2, size_t n);
static bool is_keyword_boundary(char char_before, char char_after);
// print_results is declared in header



// --- Function Implementations ---

// Initialize the parsing context (file, buffer, initial state)
bool initialize_context(ParsingContext *ctx, const char *filename) {
    ctx->fp = fopen(filename, "rb");
    if (!ctx->fp) {
        fprintf(stderr, "Error opening file '%s': %s\n", filename, strerror(errno));
        return false;
    }

    ctx->buffer_alloc_size = CHUNK_SIZE + BUFFER_EXTRA_MARGIN;
    ctx->buffer = malloc(ctx->buffer_alloc_size);
    if (!ctx->buffer) {
        perror("Failed to allocate read buffer");
        // ctx->fp is open, cleanup will handle it
        return false;
    }

    // Initialize other context members
    ctx->buffer_data_len = 0;
    ctx->global_offset = 0;
    ctx->current_line = 1;
    ctx->last_newline_offset = -1; // Start before the file begins
    ctx->state = STATE_CODE;
    ctx->index = (TableIndex){NULL, 0, 0}; // Initialize index
    ctx->error_occurred = false;

    return true;
}

// Helper to clean up just the index part
static void cleanup_context_index(TableIndex *index) {
    if (index->entries) {
        for (size_t i = 0; i < index->count; ++i) {
            free(index->entries[i].name);
        }
        free(index->entries);
        index->entries = NULL;
    }
    index->count = 0;
    index->capacity = 0;
}

// Free resources held by the context
void cleanup_context(ParsingContext *ctx) {
    if (ctx->fp) {
        fclose(ctx->fp);
        ctx->fp = NULL;
    }
    free(ctx->buffer);
    ctx->buffer = NULL;

    // Free index data
    if (ctx->index.entries) {
        for (size_t i = 0; i < ctx->index.count; ++i) {
            free(ctx->index.entries[i].name);
        }
        free(ctx->index.entries);
        ctx->index.entries = NULL;
    }
    ctx->index.count = 0;
    ctx->index.capacity = 0;
}

// Main loop for reading and processing the file
bool process_sql_file(ParsingContext *ctx) {
    size_t bytes_read;

    while (true) {
        // Read next chunk
        bytes_read = fread(ctx->buffer + ctx->buffer_data_len, 1, CHUNK_SIZE, ctx->fp);

        if (bytes_read == 0) {
            if (ferror(ctx->fp)) {
                perror("Error reading file");
                ctx->error_occurred = true;
            }
            // Break if EOF or error, but process any remaining data first
            if (ctx->buffer_data_len == 0) break; // Nothing left to process
             // else proceed to process the final partial buffer
        }

        ctx->buffer_data_len += bytes_read;

        // Process the data currently in the buffer
        size_t processed_len = process_chunk(ctx);

        if (ctx->error_occurred) {
            return false; // Stop processing on fatal error (e.g., alloc failure)
        }

        // Update global offset based on processed data
        ctx->global_offset += processed_len;

        // Shift remaining unprocessed data to the beginning
        if (processed_len < ctx->buffer_data_len) {
            size_t remaining_len = ctx->buffer_data_len - processed_len;
            memmove(ctx->buffer, ctx->buffer + processed_len, remaining_len);
            ctx->buffer_data_len = remaining_len;
        } else {
            ctx->buffer_data_len = 0; // All data processed
        }

        // If we hit EOF in the read above and processed everything, we're done.
        if (bytes_read == 0 && ctx->buffer_data_len == 0) {
            break;
        }
    }
    return !ctx->error_occurred;
}

// Process a buffer full of data, updating context state
// Returns the number of bytes processed from the start of the buffer.
// Made static as it's only called internally by process_sql_file
static size_t process_chunk(ParsingContext *ctx) {
    char *ptr = ctx->buffer;
    char *buffer_end = ctx->buffer + ctx->buffer_data_len;

    while (ptr < buffer_end && !ctx->error_occurred) {
        long long current_absolute_offset = ctx->global_offset + (ptr - ctx->buffer);
        char current_char = *ptr;
        char next_char = (ptr + 1 < buffer_end) ? *(ptr + 1) : '\0';

        // Handle state transitions and specific state logic
        if (ctx->state == STATE_CODE) {
            // Handle potential keyword match and other code-state chars
             if (!handle_code_state(ctx, &ptr, buffer_end)){
                 // If handle_code_state returns false, it means a fatal error occurred
                 break; // Exit processing loop
             }
             // handle_code_state advances ptr internally if needed
        } else {
            // Handle state transitions for comments/strings
            update_parser_state(ctx, current_char, next_char, &ptr);
        }


        // Update Line/Column Count *after* state handling for the current char
        // Need to use the offset *before* ptr might have been advanced by state handlers
        current_absolute_offset = ctx->global_offset + (ptr - ctx->buffer);
        if (*ptr == '\n') { // Check char at the potentially advanced ptr
            ctx->current_line++;
            ctx->last_newline_offset = current_absolute_offset; // Offset of the newline itself
            if (ctx->state == STATE_SL_COMMENT) { // SL comments end on newline
                 ctx->state = STATE_CODE;
             }
        }

        // Advance to the next character (if not already advanced by handlers)
        ptr++;
    }

    // Return how much of the buffer was processed
    return ptr - ctx->buffer;
}

// Updates the parser state based on current/next char, advances ptr past multi-char tokens
// Made static as it's only called internally by process_chunk
static void update_parser_state(ParsingContext *ctx, char current_char, char next_char, char **ptr_ref) {
     char *ptr = *ptr_ref; // Local copy for easier reading

    switch (ctx->state) {
         // STATE_CODE transitions are handled in handle_code_state
        case STATE_SL_COMMENT:
            // End of state handled by newline check in process_chunk
            break;
        case STATE_ML_COMMENT:
            if (current_char == '*' && next_char == '/') {
                ctx->state = STATE_CODE;
                (*ptr_ref)++; // Consume the '/' as well
            }
            break;
        case STATE_S_QUOTE_STRING:
            if (current_char == '\\') { // Skip escaped char
                (*ptr_ref)++;
            } else if (current_char == '\'') {
                if (next_char == '\'') { // Handle ''
                    (*ptr_ref)++;
                } else {
                    ctx->state = STATE_CODE;
                }
            }
            break;
        case STATE_D_QUOTE_STRING:
            if (current_char == '\\') {
                (*ptr_ref)++;
            } else if (current_char == '"') {
                 if (next_char == '"') { // Handle ""
                     (*ptr_ref)++;
                 } else {
                    ctx->state = STATE_CODE;
                 }
            }
            break;
        case STATE_BACKTICK_IDENTIFIER:
             if (current_char == '`') {
                 if (next_char == '`') { // Handle ``
                     (*ptr_ref)++;
                 } else {
                    ctx->state = STATE_CODE;
                 }
            }
            break;
        case STATE_CODE:
             // Should not be reached here, handled by handle_code_state
             fprintf(stderr, "Internal error: update_parser_state called in STATE_CODE\n");
             break;
    }
}

// Handles logic specific to STATE_CODE, including keyword detection
// Advances *ptr_ref past handled sequences. Returns true on success, false on fatal error.
// Made static as it's only called internally by process_chunk
static bool handle_code_state(ParsingContext *ctx, char **ptr_ref, char *buffer_end) {
    char *ptr = *ptr_ref;
    char current_char = *ptr;
    char next_char = (ptr + 1 < buffer_end) ? *(ptr + 1) : '\0';
    char prev_char = (ptr > ctx->buffer) ? *(ptr - 1) : '\0';
    long long current_absolute_offset = ctx->global_offset + (ptr - ctx->buffer);

    // Check for state transitions out of CODE
    if (current_char == '-' && next_char == '-') {
        ctx->state = STATE_SL_COMMENT;
        *ptr_ref += 1; // Advance past the second '-'
    } else if (current_char == '#') {
        ctx->state = STATE_SL_COMMENT;
    } else if (current_char == '/' && next_char == '*') {
        ctx->state = STATE_ML_COMMENT;
        *ptr_ref += 1; // Advance past the '*'
    } else if (current_char == '\'') {
        ctx->state = STATE_S_QUOTE_STRING;
    } else if (current_char == '"') {
        ctx->state = STATE_D_QUOTE_STRING;
    } else if (current_char == '`') {
        ctx->state = STATE_BACKTICK_IDENTIFIER;
    }
    // Check for CREATE TABLE keyword (only if no state transition occurred)
    else if (strncasecmp_custom(ptr, CREATE_TABLE_KEYWORD, CREATE_TABLE_LEN) == 0)
    {
        // Check buffer boundary for full keyword + next char
        if (ptr + CREATE_TABLE_LEN <= buffer_end) {
            char char_after_keyword = *(ptr + CREATE_TABLE_LEN);
             if (is_keyword_boundary(prev_char, char_after_keyword)) {
                // --- Found Valid "CREATE TABLE" ---
                long long keyword_start_offset = current_absolute_offset;
                long long keyword_start_line = ctx->current_line;
                // Calculate 1-based column
                long long keyword_start_col = (ctx->last_newline_offset == -1)
                                              ? keyword_start_offset + 1
                                              : keyword_start_offset - ctx->last_newline_offset;


                const char *name_parser_start = ptr + CREATE_TABLE_LEN;
                const char *name_start = NULL;
                const char *name_end = NULL;
                size_t name_len = 0;

                if (parse_table_name(name_parser_start, buffer_end, &name_start, &name_len, &name_end)) {
                     if (name_len > 0) {
                        if (!add_entry(&ctx->index, keyword_start_offset, keyword_start_line, keyword_start_col, name_start, name_len)) {
                            ctx->error_occurred = true; // Allocation error
                            return false; // Signal fatal error
                        }
                    } else {
                        fprintf(stderr, "Warning: Parsed empty table name after CREATE TABLE near offset %lld (line %lld, col %lld)\n", keyword_start_offset, keyword_start_line, keyword_start_col);
                    }
                    // Advance ptr past the name (or where parsing stopped)
                     *ptr_ref = (char*)name_end - 1; // Loop increment will move to next char
                } else {
                     // Parsing failed or no name found
                     fprintf(stderr, "Warning: Could not parse table name after CREATE TABLE near offset %lld (line %lld, col %lld)\n", keyword_start_offset, keyword_start_line, keyword_start_col);
                     // Advance ptr past "CREATE TABLE" only
                     *ptr_ref += CREATE_TABLE_LEN - 1; // Loop increment adds 1
                }
             }
             // If not boundary match, just continue loop (ptr increments normally)
        } else {
             // Keyword potentially truncated at buffer end - will be handled in next chunk
             // Do nothing special, let buffer shifting handle it.
        }
    }
    // If none of the above, ptr will be incremented normally by the caller loop

    return true; // No fatal error occurred in this function
}


// Parses table name after 'CREATE TABLE', returns true if name syntax seems valid (even if empty)
// Outputs name details and advances end_ptr_out to the position *after* the parsed name.
// Made static as it's only called internally by handle_code_state
static bool parse_table_name(const char *start_ptr, const char *limit, const char **name_start_out, size_t *name_len_out, const char **end_ptr_out) {
    const char *parser = start_ptr;
    const char *name_start = NULL;
    const char *name_end = NULL;
    char quote_char = 0;
    bool name_found_potentially = false;

    // Skip whitespace
    while (parser < limit && isspace((unsigned char)*parser)) {
        parser++;
    }

    if (parser >= limit) {
        *end_ptr_out = parser;
        return false; // Reached end before finding anything
    }

    // Check for quoting
    if (*parser == '`' || *parser == '"' || *parser == '[') {
        quote_char = (*parser == '[') ? ']' : *parser;
        parser++; // Move past opening quote
        name_start = parser;
        while (parser < limit) {
            if (*parser == quote_char) {
                // Handle escaped quotes
                if (quote_char != '[' && parser + 1 < limit && *(parser + 1) == quote_char) {
                    parser++; // Skip the second quote of the pair
                } else {
                    name_end = parser; // Found end quote
                    parser++; // Move past end quote
                    name_found_potentially = true;
                    break;
                }
            }
            parser++;
        }
         if (!name_found_potentially && parser >= limit) {
              fprintf(stderr, "Warning: Unterminated quoted identifier starting near offset %ld\n", (long)(name_start - limit)); // Approximate offset
              // Treat as potentially incomplete, but report failure to parse fully
              *name_start_out = name_start;
              *name_len_out = limit - name_start;
              *end_ptr_out = limit;
              return false; // Indicate parsing didn't complete cleanly
         }

    } else if (isalnum((unsigned char)*parser) || *parser == '_') { // Start of unquoted name
        name_start = parser;
        while (parser < limit && (isalnum((unsigned char)*parser) || *parser == '_' || *parser == '.' || *parser == '$')) {
             parser++;
        }
        name_end = parser;
        name_found_potentially = true; // Found end of unquoted name sequence
    } else {
         // Didn't find quoting or valid unquoted start char
         *end_ptr_out = parser; // End where we stopped
         return false; // Indicate no valid name started here
    }

    // Final checks and output
    if (name_found_potentially && name_start) {
         *name_start_out = name_start;
         *name_len_out = (name_end > name_start) ? (name_end - name_start) : 0;
         *end_ptr_out = parser; // Position after the parsed name/quote
         return true; // Indicate success (even if length is 0)
    } else {
         // Should only happen if unterminated quoted name hit limit exactly
         *end_ptr_out = parser;
         return false; // Indicate parsing failure
    }
}


// Add entry to index (dynamic array resizing) - Returns true on success
// Made static as it's only called internally by handle_code_state
static bool add_entry(TableIndex *index, long long offset, long long line, long long col, const char *name_start, size_t name_len) {
    if (index->count >= index->capacity) {
        size_t new_capacity = index->capacity == 0 ? 16 : index->capacity * 2;
        if (new_capacity < index->capacity) {
             fprintf(stderr, "Error: Index capacity overflow\n");
             return false;
        }
        TableEntry *new_entries = realloc(index->entries, new_capacity * sizeof(TableEntry));
        if (!new_entries) {
            perror("Failed to reallocate memory for index entries");
            return false;
        }
        index->entries = new_entries;
        index->capacity = new_capacity;
    }

    char *name_copy = malloc(name_len + 1);
    if (!name_copy) {
        perror("Failed to allocate memory for table name");
        return false;
    }
    memcpy(name_copy, name_start, name_len);
    name_copy[name_len] = '\0';

    index->entries[index->count].offset = offset;
    index->entries[index->count].line = line;
    index->entries[index->count].column = col;
    index->entries[index->count].name = name_copy;
    index->count++;
    return true;
}

// Case-insensitive string comparison (simple version)
// Made static as it's only called internally by handle_code_state
static int strncasecmp_custom(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    while (n-- > 0 && tolower((unsigned char)*s1) == tolower((unsigned char)*s2)) {
        if (n == 0 || *s1 == '\0') break; // Matched n chars or end of string
        s1++;
        s2++;
    }
    return (n == (size_t)-1) ? 0 : (tolower((unsigned char)*s1) - tolower((unsigned char)*s2));
}


// Check boundary characters for the keyword
// Made static as it's only called internally by handle_code_state
static bool is_keyword_boundary(char char_before, char char_after) {
    bool start_ok = (char_before == '\0' || isspace((unsigned char)char_before) || strchr(";(/*", char_before) != NULL);
    if (!start_ok) return false;
    bool end_ok = (char_after == '\0' || isspace((unsigned char)char_after) || char_after == '(');
    return end_ok;
}

// Check if index file exists for given SQL file
bool index_file_exists(const char *sql_filename) {
    char index_filename[1024];
    snprintf(index_filename, sizeof(index_filename), "%s.index", sql_filename);
    FILE *fp = fopen(index_filename, "rb");
    if (fp) {
        fclose(fp);
        return true;
    }
    return false;
}

// Save index to file
bool save_index_to_file(const TableIndex *index, const char *sql_filename) {
    char index_filename[1024];
    snprintf(index_filename, sizeof(index_filename), "%s.index", sql_filename);
    
    FILE *fp = fopen(index_filename, "wb");
    if (!fp) {
        perror("Failed to open index file for writing");
        return false;
    }

    // Write header with version and count
    const char header[] = "SQLIDX1";
    if (fwrite(header, 1, sizeof(header)-1, fp) != sizeof(header)-1) {
        perror("Failed to write header");
        fclose(fp);
        return false;
    }

    // Write entry count
    if (fwrite(&index->count, sizeof(size_t), 1, fp) != 1) {
        perror("Failed to write entry count");
        fclose(fp);
        return false;
    }

    // Write each entry
    for (size_t i = 0; i < index->count; i++) {
        const TableEntry *entry = &index->entries[i];
        
        // Write fixed-size fields
        if (fwrite(&entry->offset, sizeof(entry->offset), 1, fp) != 1 ||
            fwrite(&entry->line, sizeof(entry->line), 1, fp) != 1 ||
            fwrite(&entry->column, sizeof(entry->column), 1, fp) != 1) {
            perror("Failed to write entry data");
            fclose(fp);
            return false;
        }

        // Write name length and name
        size_t name_len = strlen(entry->name);
        if (fwrite(&name_len, sizeof(size_t), 1, fp) != 1 ||
            fwrite(entry->name, 1, name_len, fp) != name_len) {
            perror("Failed to write name data");
            fclose(fp);
            return false;
        }
    }

    fclose(fp);
    return true;
}

// Load index from file
bool load_index_from_file(TableIndex *index, const char *sql_filename) {
    char index_filename[1024];
    snprintf(index_filename, sizeof(index_filename), "%s.index", sql_filename);
    
    FILE *fp = fopen(index_filename, "rb");
    if (!fp) {
        perror("Failed to open index file for reading");
        return false;
    }

    // Read and verify header
    char header[8];
    if (fread(header, 1, sizeof(header)-1, fp) != sizeof(header)-1 ||
        memcmp(header, "SQLIDX1", sizeof(header)-1) != 0) {
        fprintf(stderr, "Invalid index file format\n");
        fclose(fp);
        return false;
    }

    // Read entry count
    size_t count;
    if (fread(&count, sizeof(size_t), 1, fp) != 1) {
        perror("Failed to read entry count");
        fclose(fp);
        return false;
    }

    // Initialize index
    index->entries = malloc(count * sizeof(TableEntry));
    if (!index->entries) {
        perror("Failed to allocate memory for index entries");
        fclose(fp);
        return false;
    }
    index->count = 0;
    index->capacity = count;

    // Read each entry
    for (size_t i = 0; i < count; i++) {
        TableEntry *entry = &index->entries[i];
        
        // Read fixed-size fields
        if (fread(&entry->offset, sizeof(entry->offset), 1, fp) != 1 ||
            fread(&entry->line, sizeof(entry->line), 1, fp) != 1 ||
            fread(&entry->column, sizeof(entry->column), 1, fp) != 1) {
            perror("Failed to read entry data");
            cleanup_context_index(index);
            fclose(fp);
            return false;
        }

        // Read name length and name
        size_t name_len;
        if (fread(&name_len, sizeof(size_t), 1, fp) != 1) {
            perror("Failed to read name length");
            cleanup_context_index(index);
            fclose(fp);
            return false;
        }

        entry->name = malloc(name_len + 1);
        if (!entry->name) {
            perror("Failed to allocate memory for table name");
            cleanup_context_index(index);
            fclose(fp);
            return false;
        }

        if (fread(entry->name, 1, name_len, fp) != name_len) {
            perror("Failed to read name data");
            free(entry->name);
            cleanup_context_index(index);
            fclose(fp);
            return false;
        }
        entry->name[name_len] = '\0';
        index->count++;
    }

    fclose(fp);
    return true;
}

// Print the indexed results
void print_results(const TableIndex *index) {
    printf("Found 'CREATE TABLE' statements (Top Level Only):\n");
    printf("%-15s %-10s %-10s %s\n", "Offset", "Line", "Column", "Table Name");
    printf("--------------------------------------------------\n");
    if (index->count > 0) {
        for (size_t i = 0; i < index->count; ++i) {
            printf("%-15lld %-10lld %-10lld %s\n",
                   index->entries[i].offset,
                   index->entries[i].line,
                   index->entries[i].column, // Stored as 1-based
                   index->entries[i].name);
        }
    } else {
        printf("No top-level 'CREATE TABLE' statements found or names could not be parsed.\n");
    }
}
