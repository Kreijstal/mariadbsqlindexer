#include "sql_indexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h> // For bool type
#include <unistd.h> // For access()

// --- Global Verbose Flag Definition ---
bool verbose_mode = false;

// --- Static Helper Function Declarations ---
// Function to print usage instructions
void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-v] [--dump-table <name>] <sql_file>\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  <sql_file>        : Path to the SQL file to process.\n");
    fprintf(stderr, "  -v, --verbose     : Enable verbose debug messages.\n");
    fprintf(stderr, "  --dump-table <name> : Dump a specific table to JSON and exit.\n\n");
    fprintf(stderr, "Indexing Behavior:\n");
    fprintf(stderr, "  - Automatically loads '<sql_file>.index' if it exists and SHA256 matches.\n");
    fprintf(stderr, "  - Automatically saves index to '<sql_file>.index' after parsing.\n");
}

int main(int argc, char *argv[]) {
    const char *sql_filename = NULL;
    char *index_filename = NULL; // Dynamically allocated
    bool load_from_index = false;
    bool write_to_index = false;
    const char *dump_table_name = NULL;

    // --- Argument Parsing ---
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose_mode = true;
        } else if (strcmp(argv[i], "--dump-table") == 0) {
            if (i + 1 < argc) {
                dump_table_name = argv[++i];
            } else {
                fprintf(stderr, "Error: --dump-table requires a table name.\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            if (sql_filename == NULL) {
                sql_filename = argv[i];
            } else {
                fprintf(stderr, "Error: Multiple SQL files specified ('%s' and '%s'). Only one is allowed.\n", sql_filename, argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    if (verbose_mode) {
        DEBUG_PRINT("Verbose mode enabled.");
    }

    if (sql_filename == NULL) {
        fprintf(stderr, "Error: SQL file path is required.\n");
        print_usage(argv[0]);
        return 1;
    }

    // --- Determine Index Filename ---
    size_t sql_len = strlen(sql_filename);
    index_filename = malloc(sql_len + 7); // + ".index" + null terminator
    if (!index_filename) {
        perror("Error allocating memory for index filename");
        return 1;
    }
    sprintf(index_filename, "%s.index", sql_filename);

    ParsingContext ctx = {0};
    SqlIndex index = {0};
    bool success = true;

    // --- Index Loading/Parsing Logic ---
    char current_sha[65] = {0};

    if (access(index_filename, F_OK) == 0) {
        DEBUG_PRINT("Index file '%s' exists. Attempting to load.", index_filename);
        if (read_index_from_file(&index, index_filename)) {
            if (index.sql_file_sha256[0] != '\0') {
                if (calculate_sha256(sql_filename, current_sha)) {
                    if (strcmp(index.sql_file_sha256, current_sha) == 0) {
                        DEBUG_PRINT("SHA256 match. Using existing index.");
                        load_from_index = true;
                    } else {
                        DEBUG_PRINT("SHA256 mismatch. Re-parsing SQL file.");
                        cleanup_index(&index);
                        write_to_index = true;
                    }
                } else {
                    fprintf(stderr, "Warning: Could not calculate SHA256 for '%s'. Re-parsing.\n", sql_filename);
                    cleanup_index(&index);
                    write_to_index = true;
                }
            } else {
                 DEBUG_PRINT("No SHA256 in index file. Using index without verification.");
                 load_from_index = true;
            }
        } else {
            fprintf(stderr, "Error loading index file '%s'. Re-parsing.\n", index_filename);
            write_to_index = true;
        }
    } else {
        DEBUG_PRINT("Index file '%s' not found. Will parse SQL and save index.", index_filename);
        write_to_index = true;
    }

    if (!load_from_index) {
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
                index = ctx.index;
                ctx.index.entries = NULL; // Prevent double free
                ctx.index.count = 0;
                ctx.index.capacity = 0;

                if (write_to_index) {
                    DEBUG_PRINT("Writing index to %s", index_filename);
                    // Calculate hash if not already calculated
                    if (current_sha[0] == '\0') {
                        calculate_sha256(sql_filename, current_sha);
                    }
                    if (!write_index_to_file(&index, index_filename, current_sha)) {
                        fprintf(stderr, "Error writing index file '%s'.\n", index_filename);
                    } else {
                        DEBUG_PRINT("Index written successfully.");
                    }
                }
            }
            cleanup_context(&ctx);
        }
    }

    if (success) {
        if (dump_table_name) {
            DEBUG_PRINT("Dumping table '%s' as JSON.", dump_table_name);
            dump_table_as_json(&index, dump_table_name, sql_filename);
        } else {
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
    }

    // Cleanup the index structure (if loaded or successfully parsed)
    DEBUG_PRINT("Cleaning up index structure.");
    cleanup_index(&index);

    // Free the dynamically allocated index filename
    free(index_filename);

    DEBUG_PRINT("Exiting %s.", success ? "successfully" : "with errors");
    return success ? 0 : 1;
}

// --- Static Helper Function Implementations ---