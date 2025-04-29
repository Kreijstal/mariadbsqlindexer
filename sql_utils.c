#include "sql_utils.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Case-insensitive string comparison (simple version)
int strncasecmp_custom(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    while (n-- > 0 && tolower((unsigned char)*s1) == tolower((unsigned char)*s2)) {
        if (n == 0 || *s1 == '\0') break;
        s1++;
        s2++;
    }
    return (n == (size_t)-1) ? 0 : (tolower((unsigned char)*s1) - tolower((unsigned char)*s2));
}

// Check boundary characters for the keyword
bool is_keyword_boundary(char char_before, char char_after) {
    bool start_ok = (char_before == '\0' || isspace((unsigned char)char_before) || 
                    strchr(";(/*", char_before) != NULL);
    if (!start_ok) return false;
    bool end_ok = (char_after == '\0' || isspace((unsigned char)char_after) || 
                  char_after == '(');
    return end_ok;
}

// Parses table name after 'CREATE TABLE'
bool parse_table_name(const char *start_ptr, const char *limit, 
                     const char **name_start_out, size_t *name_len_out, 
                     const char **end_ptr_out) {
    const char *parser = start_ptr;
    const char *name_start = NULL;
    const char *name_end = NULL;
    char quote_char = 0;
    bool name_found = false;

    // Skip whitespace
    while (parser < limit && isspace((unsigned char)*parser)) {
        parser++;
    }

    if (parser >= limit) {
        *end_ptr_out = parser;
        return false;
    }

    // Check for quoting
    if (*parser == '`' || *parser == '"' || *parser == '[') {
        quote_char = (*parser == '[') ? ']' : *parser;
        parser++;
        name_start = parser;
        while (parser < limit) {
            if (*parser == quote_char) {
                if (quote_char != '[' && parser + 1 < limit && *(parser + 1) == quote_char) {
                    parser++;
                } else {
                    name_end = parser;
                    parser++;
                    name_found = true;
                    break;
                }
            }
            parser++;
        }
    } 
    // ... (rest of implementation)
    return name_found;
}

// Add entry to index (dynamic array resizing)
bool add_entry(TableIndex *index, long long offset, long long line, 
              long long col, const char *name_start, size_t name_len) {
    if (index->count >= index->capacity) {
        size_t new_capacity = index->capacity == 0 ? 16 : index->capacity * 2;
        TableEntry *new_entries = realloc(index->entries, new_capacity * sizeof(TableEntry));
        if (!new_entries) return false;
        index->entries = new_entries;
        index->capacity = new_capacity;
    }

    char *name_copy = malloc(name_len + 1);
    if (!name_copy) return false;
    memcpy(name_copy, name_start, name_len);
    name_copy[name_len] = '\0';

    index->entries[index->count] = (TableEntry){
        .offset = offset,
        .line = line,
        .column = col,
        .name = name_copy
    };
    index->count++;
    return true;
}