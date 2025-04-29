#ifndef SQL_UTILS_H
#define SQL_UTILS_H

#include "sql_indexer.h"
#include <stdbool.h>

// String utilities
int strncasecmp_custom(const char *s1, const char *s2, size_t n);
bool is_keyword_boundary(char char_before, char char_after);

// Parsing utilities
bool parse_table_name(const char *start_ptr, const char *limit, 
                     const char **name_start_out, size_t *name_len_out, 
                     const char **end_ptr_out);

// Index utilities
bool add_entry(TableIndex *index, long long offset, long long line, 
              long long col, const char *name_start, size_t name_len);

#endif // SQL_UTILS_H