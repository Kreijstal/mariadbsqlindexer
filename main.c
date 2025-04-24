#include "sql_indexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h> // For bool type
#include <unistd.h> // For access()

// --- Global Verbose Flag Definition ---
bool verbose_mode = false;

// Function to print usage instructions
void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-v] <sql_file>\n", prog_name);
    fprintf(stderr, "  <sql_file> : Path to the SQL file to process.\n");
    fprintf(stderr, "  -v         : Enable verbose debug messages.\n");
    fprintf(stderr, "               Automatically loads '<sql_file>.index' if it exists.\n");
    fprintf(stderr, "               Automatically saves index to '<sql_file>.index' after parsing.\n");
}

int main(int argc, char *argv[]) {
    const char *sql_filename = NULL;
    char *index_filename = NULL; // Dynamically allocated
    const char *load_index_filename = NULL; // Points to index_filename if loading
    const char *output_index_filename = NULL; // Points to index_filename if saving

    // --- Argument Parsing ---
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose_mode = true;
            // Delay DEBUG_PRINT until after all args parsed
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            // Assume it's the SQL filename
            if (sql_filename == NULL) {
                sql_filename = argv[i];
            } else {
                fprintf(stderr, "Error: Multiple SQL files specified ('%s' and '%s'). Only one is allowed.\n", sql_filename, argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    // Now that parsing is done, we can use DEBUG_PRINT
    if (verbose_mode) {
        DEBUG_PRINT("Verbose mode enabled.");
    }

    // Check if SQL file is provided
    if (sql_filename == NULL) {
        fprintf(stderr, "Error: SQL file path is required.\n");
        print_usage(argv[0]);
        return 1;
    }

    // --- Determine Index Filename and Action ---
    size_t sql_len = strlen(sql_filename);
    index_filename = malloc(sql_len + 7); // + ".index" + null terminator
    if (!index_filename) {
        perror("Error allocating memory for index filename");
        return 1;
    }
    sprintf(index_filename, "%s.index", sql_filename);

    // Check if the index file exists using access() from unistd.h
    if (access(index_filename, F_OK) == 0) {
        // File exists, try to load it
        load_index_filename = index_filename;
        DEBUG_PRINT("Index file '%s' exists. Attempting to load.", index_filename);
    } else {
        // File does not exist, parse SQL and save index
        output_index_filename = index_filename;
        DEBUG_PRINT("Index file '%s' not found. Will parse SQL and save index.", index_filename);
    }


    DEBUG_PRINT("SQL file: %s", sql_filename);
    DEBUG_PRINT("Index file: %s (%s)", index_filename, load_index_filename ? "load" : "save");


    ParsingContext ctx = {0}; // Initialize context
    SqlIndex index = {0}; // Initialize index separately
    bool success = true;

    if (load_index_filename) {
        DEBUG_PRINT("Loading index from %s", load_index_filename);
        if (!read_index_from_file(&index, load_index_filename)) {
            fprintf(stderr, "Error loading index file '%s'.\n", load_index_filename);
            success = false;
        } else {
             DEBUG_PRINT("Index loaded successfully. Count: %d", index.count);
        }
    } else {
        // No index file to load, parse the SQL file
        DEBUG_PRINT("Initializing context for parsing %s", sql_filename);
        if (!initialize_context(&ctx, sql_filename)) {
            fprintf(stderr, "Error initializing context for file '%s'.\n", sql_filename);
            success = false;
        } else {
            DEBUG_PRINT("Context initialized. Starting file processing.");
            if (!process_sql_file(&ctx)) {
                fprintf(stderr, "Error processing SQL file '%s'.\n", sql_filename);
                success = false;
            } else {
                DEBUG_PRINT("File processing finished. Index count: %d", ctx.index.count);
                // Move the index from context to the separate index variable
                index = ctx.index;
                // Prevent cleanup_context from freeing the index we just moved
                ctx.index.entries = NULL;
                ctx.index.count = 0;
                ctx.index.capacity = 0;

                // Save the newly created index if output_index_filename is set
                if (output_index_filename) {
                    DEBUG_PRINT("Writing index to %s", output_index_filename);
                    if (!write_index_to_file(&index, output_index_filename)) {
                        fprintf(stderr, "Error writing index file '%s'.\n", output_index_filename);
                        // Continue even if writing fails, maybe user wants to see output
                    } else {
                        DEBUG_PRINT("Index written successfully.");
                    }
                }
            }
            // Cleanup context (file handle, buffer) regardless of success
            DEBUG_PRINT("Cleaning up parsing context.");
            cleanup_context(&ctx);
        }
    }

    if (success) {
        DEBUG_PRINT("Printing results.");
        print_results(&index);

        // --- Example: Get sample for the first table ---
        if (index.count > 0 && index.entries[0].table_info && sql_filename) {
             DEBUG_PRINT("Attempting to get sample row for table: %s", index.entries[0].table_info->name);
             char* sample = get_first_row_sample(sql_filename, index.entries[0].table_info->end_offset, index.entries[0].table_info->name);
             if (sample) {
                 printf("\n--- Sample First Row for %s (Offset: %ld) ---\n", index.entries[0].table_info->name, index.entries[0].table_info->end_offset);
                 printf("%s\n", sample);
                 printf("------------------------------------------\n");
                 free(sample);
             } else {
                 DEBUG_PRINT("Could not get sample row for table: %s", index.entries[0].table_info->name);
             }
        }
        // TODO: Add ncurses UI call here later
        // display_table_columns_ui(&index);
    }

    // Cleanup the index structure (if loaded or successfully parsed)
    DEBUG_PRINT("Cleaning up index structure.");
    cleanup_index(&index);

    // Free the dynamically allocated index filename
    free(index_filename);

    DEBUG_PRINT("Exiting %s.", success ? "successfully" : "with errors");
    return success ? 0 : 1;
}