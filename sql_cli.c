#include "sql_cli.h"
#include "sql_utils.h"
#include <string.h>
#include <stdlib.h>

CLI_Context* init_cli(TableIndex *index) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    CLI_Context *ctx = malloc(sizeof(CLI_Context));
    if (!ctx) return NULL;

    ctx->index = index;
    ctx->selected_table = 0;

    // Create main window
    ctx->main_win = newwin(LINES, COLS, 0, 0);
    box(ctx->main_win, 0, 0);
    
    // Create table selection window
    ctx->table_win = derwin(ctx->main_win, LINES-4, COLS/2, 2, 1);
    keypad(ctx->table_win, TRUE);
    
    // Create info window
    ctx->info_win = derwin(ctx->main_win, LINES-4, COLS/2-2, 2, COLS/2+1);
    scrollok(ctx->info_win, TRUE);

    refresh();
    wrefresh(ctx->main_win);
    return ctx;
}

void cleanup_cli(CLI_Context *ctx) {
    if (!ctx) return;
    delwin(ctx->table_win);
    delwin(ctx->info_win);
    delwin(ctx->main_win);
    endwin();
    free(ctx);
}

void draw_table_list(CLI_Context *ctx) {
    werase(ctx->table_win);
    for (size_t i = 0; i < ctx->index->count; i++) {
        if (i == ctx->selected_table) {
            wattron(ctx->table_win, A_REVERSE);
        }
        mvwprintw(ctx->table_win, i+1, 1, "%s", ctx->index->entries[i].name);
        if (i == ctx->selected_table) {
            wattroff(ctx->table_win, A_REVERSE);
        }
    }
    box(ctx->table_win, 0, 0);
    wrefresh(ctx->table_win);
}

void show_table_info(CLI_Context *ctx) {
    werase(ctx->info_win);
    if (ctx->index->count > 0) {
        TableEntry *entry = &ctx->index->entries[ctx->selected_table];
        mvwprintw(ctx->info_win, 1, 1, "Table: %s", entry->name);
        mvwprintw(ctx->info_win, 2, 1, "Location:");
        mvwprintw(ctx->info_win, 3, 1, "  Line: %lld", entry->line);
        mvwprintw(ctx->info_win, 4, 1, "  Column: %lld", entry->column);
        mvwprintw(ctx->info_win, 5, 1, "  Offset: %lld", entry->offset);
    }
    box(ctx->info_win, 0, 0);
    wrefresh(ctx->info_win);
}

void run_cli(CLI_Context *ctx) {
    int ch;
    bool running = true;

    while (running) {
        draw_table_list(ctx);
        show_table_info(ctx);

        ch = wgetch(ctx->table_win);
        switch (ch) {
            case KEY_UP:
                if (ctx->selected_table > 0) ctx->selected_table--;
                break;
            case KEY_DOWN:
                if (ctx->selected_table < ctx->index->count - 1) ctx->selected_table++;
                break;
            case '\n': // Enter
                // TODO: Parse selected table's columns
                running = false;
                break;
            case 'q':
                running = false;
                break;
        }
    }
}