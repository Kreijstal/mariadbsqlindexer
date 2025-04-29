#ifndef SQL_CLI_H
#define SQL_CLI_H

#include "sql_indexer.h"
#include <ncursesw/ncurses.h>

typedef struct {
    WINDOW *main_win;
    WINDOW *table_win;
    WINDOW *info_win;
    int selected_table;
    TableIndex *index;
} CLI_Context;

// Initialize ncurses interface
CLI_Context* init_cli(TableIndex *index);

// Run the main CLI loop
void run_cli(CLI_Context *ctx);

// Clean up ncurses resources
void cleanup_cli(CLI_Context *ctx);

#endif // SQL_CLI_H