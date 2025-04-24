#include "sql_indexer.h"
#include <stdio.h>
#include <stdlib.h> // For EXIT_FAILURE, EXIT_SUCCESS
#include <string.h> // For strcat, strcpy, strlen
#include <unistd.h> // For access()
#include <stdbool.h> // For bool type

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <sql_filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename = argv[1];
    char *index_filename = NULL;
    ParsingContext ctx = {0}; // Initialize context members to zero/NULL
    bool index_loaded_from_file = false;

    // Create index filename (<sql_filename>.index)
    index_filename = malloc(strlen(filename) + strlen(".index") + 1);
    if (!index_filename) {
        perror("Failed to allocate memory for index filename");
        return EXIT_FAILURE;
    }
    strcpy(index_filename, filename);
    strcat(index_filename, ".index");

    // Check if index file exists
    if (access(index_filename, F_OK) == 0) {
        printf("Index file '%s' found. Loading index from file.\n", index_filename);
        // TODO: Implement read_index_from_file in sql_indexer.c/h
        if (read_index_from_file(&ctx.index, index_filename)) {
             index_loaded_from_file = true;
        } else {
            fprintf(stderr, "Error reading index file '%s'. Re-indexing.\n", index_filename);
            // Proceed to index the original SQL file if reading fails
            cleanup_index(&ctx.index); // Clean up partially loaded index
        }
    }

    if (!index_loaded_from_file) {
        printf("Index file not found or failed to load. Indexing '%s'.\n", filename);
        // Initialize context (opens file, allocates buffer)
        if (!initialize_context(&ctx, filename)) {
            cleanup_context(&ctx); // Clean up partially initialized context
            free(index_filename);
            return EXIT_FAILURE;
        }

        // Process the file
        bool success = process_sql_file(&ctx);

        // Write index to file if processing was successful
        if (success) {
            printf("Writing index to '%s'.\n", index_filename);
            // TODO: Implement write_index_to_file in sql_indexer.c/h
            if (!write_index_to_file(&ctx.index, index_filename)) {
                fprintf(stderr, "Warning: Failed to write index file '%s'.\n", index_filename);
                // Continue execution even if writing fails, but maybe log or warn.
            }
        } else if (ctx.error_occurred) {
             fprintf(stderr, "Indexing failed, index file will not be created.\n");
        }


        // Print results if processing completed successfully or if loaded from file
        if (success || !ctx.error_occurred) {
             print_results(&ctx.index);
        }

        // Clean up resources used for processing
        cleanup_context(&ctx); // Cleans up file handle and buffer

    } else {
        // Index was loaded from file, just print results
        print_results(&ctx.index);
        // Clean up index structure loaded from file
        cleanup_index(&ctx.index);
    }


    free(index_filename); // Free allocated memory for index filename

    // Use ctx.error_occurred only if we actually processed the file
    return (index_loaded_from_file || !ctx.error_occurred) ? EXIT_SUCCESS : EXIT_FAILURE;
}