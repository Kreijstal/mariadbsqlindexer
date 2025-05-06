#ifndef SQL_CLI_H
#define SQL_CLI_H

#include "sql_indexer.h"

typedef struct {
    int selected_table;
    TableIndex *index;
} CLI_Context;

// Initialize CLI interface
CLI_Context* init_cli(TableIndex *index);

// Run the main REPL loop
void run_cli(CLI_Context *ctx);

// Clean up CLI resources
void cleanup_cli(CLI_Context *ctx);

#endif // SQL_CLI_H