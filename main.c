#include "sql_indexer.h"
#include <stdio.h>
#include <stdlib.h> // For EXIT_FAILURE, EXIT_SUCCESS

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <sql_filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename = argv[1];
    ParsingContext ctx;

    // Initialize context (opens file, allocates buffer)
    if (!initialize_context(&ctx, filename)) {
        cleanup_context(&ctx); // Clean up partially initialized context
        return EXIT_FAILURE;
    }

    // Check if index file exists
    bool success;
    if (index_file_exists(filename)) {
        // Load existing index
        success = load_index_from_file(&ctx.index, filename);
        if (!success) {
            fprintf(stderr, "Failed to load index from file, falling back to processing\n");
            success = process_sql_file(&ctx);
        }
    } else {
        // Process the file and create new index
        success = process_sql_file(&ctx);
        if (success) {
            // Save index for future use
            if (!save_index_to_file(&ctx.index, filename)) {
                fprintf(stderr, "Warning: Failed to save index to file\n");
            }
        }
    }

    // Print results even if there were non-fatal errors during processing
    if (success || !ctx.error_occurred) { // Print if processing completed or error was non-fatal
        print_results(&ctx.index);
    }

    // Clean up resources
    cleanup_context(&ctx);

    return ctx.error_occurred ? EXIT_FAILURE : EXIT_SUCCESS;
}