#include "sql_cli.h"
#include "sql_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT 256
#define PAGE_SIZE 20

CLI_Context* init_cli(TableIndex *index) {
    CLI_Context *ctx = malloc(sizeof(CLI_Context));
    if (!ctx) return NULL;

    ctx->index = index;
    ctx->selected_table = 0;
    return ctx;
}

void cleanup_cli(CLI_Context *ctx) {
    if (!ctx) return;
    free(ctx);
}

static void print_tables(CLI_Context *ctx, int start, int count) {
    int end = start + count;
    if (end > ctx->index->count) end = ctx->index->count;

    printf("\nAvailable tables:\n");
    for (int i = start; i < end; i++) {
        printf("%2d. %s\n", i+1, ctx->index->entries[i].name);
    }
    printf("\n");
}

static void print_table_info(CLI_Context *ctx, int idx) {
    if (idx < 0 || idx >= ctx->index->count) return;
    
    TableEntry *entry = &ctx->index->entries[idx];
    printf("\nTable: %s\n", entry->name);
    printf("Location:\n");
    printf("  Line: %lld\n", entry->line);
    printf("  Column: %lld\n", entry->column);
    printf("  Offset: %lld\n\n", entry->offset);
}

void run_cli(CLI_Context *ctx) {
    char input[MAX_INPUT];
    int page = 0;
    
    printf("SQL Indexer REPL - Type 'help' for commands\n");
    
    while (1) {
        printf("> ");
        if (!fgets(input, MAX_INPUT, stdin)) break;
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "list") == 0) {
            print_tables(ctx, page * PAGE_SIZE, PAGE_SIZE);
            printf("Page %d - Type 'more' for next page or 'back' for previous\n", page+1);
        }
        else if (strcmp(input, "more") == 0) {
            if ((page + 1) * PAGE_SIZE < ctx->index->count) {
                page++;
                print_tables(ctx, page * PAGE_SIZE, PAGE_SIZE);
                printf("Page %d\n", page+1);
            } else {
                printf("No more tables to show\n");
            }
        }
        else if (strcmp(input, "back") == 0) {
            if (page > 0) {
                page--;
                print_tables(ctx, page * PAGE_SIZE, PAGE_SIZE);
                printf("Page %d\n", page+1);
            } else {
                printf("Already at first page\n");
            }
        }
        else if (strncmp(input, "select ", 7) == 0) {
            int idx = atoi(input + 7) - 1;
            if (idx >= 0 && idx < ctx->index->count) {
                ctx->selected_table = idx;
                print_table_info(ctx, idx);
            } else {
                printf("Invalid table number\n");
            }
        }
        else if (strcmp(input, "help") == 0) {
            printf("\nAvailable commands:\n");
            printf("list    - Show tables\n");
            printf("more    - Show next page\n");
            printf("back    - Show previous page\n");
            printf("select N - Select table N\n");
            printf("quit    - Exit the program\n\n");
        }
        else if (strcmp(input, "quit") == 0) {
            break;
        }
        else {
            printf("Unknown command. Type 'help' for available commands\n");
        }
    }
}