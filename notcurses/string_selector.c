#define _XOPEN_SOURCE 700 // Or try _DEFAULT_SOURCE if _XOPEN_SOURCE doesn't work

#include <locale.h>       // For setlocale
#include <wchar.h>        // For wcwidth, wcswidth - MUST BE EARLY

#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char* ITEMS[] = {
    "Aardvark", "Alpaca", "Ant", "Antelope", "Ape",
    "Armadillo", "Baboon", "Badger", "Bat", "Bear",
    "Beaver", "Bee", "Bison", "Boar", "Buffalo",
    "Butterfly", "Camel", "Capybara", "Caribou", "Cassowary",
    "Cat", "Caterpillar", "Cattle", "Chamois", "Cheetah",
    "Chicken", "Chimpanzee", "Chinchilla", "Chough", "Clam",
    "Cobra", "Cockroach", "Cod", "Cormorant", "Coyote",
    "Crab", "Crane", "Crocodile", "Crow", "Curlew",
    "Deer", "Dinosaur", "Dog", "Dogfish", "Dolphin",
    "Donkey", "Dotterel", "Dove", "Dragonfly", "Duck",
    "Dugong", "Dunlin", "Eagle", "Echidna", "Eel",
    "Eland", "Elephant", "Elk", "Emu", "Falcon",
    "Ferret", "Finch", "Fish", "Flamingo", "Fly",
    "Fox", "Frog", "Gaur", "Gazelle", "Gerbil",
    "Giant Panda", "Giraffe", "Gnat", "Gnu", "Goat",
    "Goldfinch", "Goldfish", "Goose", "Gorilla", "Goshawk",
    "Grasshopper", "Grouse", "Guanaco", "Gull", "Hamster",
    "Hare", "Hawk", "Hedgehog", "Heron", "Herring",
    "Hippopotamus", "Hornet", "Horse", "Human", "Hummingbird",
    "Hyena", "Ibex", "Ibis", "Jackal", "Jaguar",
    "Jay", "Jellyfish", "Kangaroo", "Kingfisher", "Koala",
    "Kookaburra", "Kouprey", "Kudu", "Lapwing", "Lark",
    "Lemur", "Leopard", "Lion", "Llama", "Lobster",
    "Locust", "Loris", "Louse", "Lyrebird", "Magpie",
    "Mallard", "Manatee", "Mandrill", "Mantis", "Marten",
    "Meerkat", "Mink", "Mole", "Mongoose", "Monkey",
    "Moose", "Mosquito", "Mouse", "Mule", "Narwhal",
    "Newt", "Nightingale", "Octopus", "Okapi", "Opossum",
    "Oryx", "Ostrich", "Otter", "Owl", "Oyster",
    "Panther", "Parrot", "Partridge", "Peafowl", "Pelican",
    "Penguin", "Pheasant", "Pig", "Pigeon", "Pony",
    "Porcupine", "Porpoise", "Quail", "Quelea", "Quetzal",
    "Rabbit", "Raccoon", "Rail", "Ram", "Rat",
    "Raven", "Red deer", "Red panda", "Reindeer", "Rhinoceros",
    "Rook", "Salamander", "Salmon", "Sand Dollar", "Sandpiper",
    "Sardine", "Scorpion", "Seahorse", "Seal", "Shark",
    "Sheep", "Shrew", "Skunk", "Snail", "Snake",
    "Sparrow", "Spider", "Spoonbill", "Squid", "Squirrel",
    "Starling", "Stingray", "Stinkbug", "Stork", "Swallow",
    "Swan", "Tapir", "Tarsier", "Termite", "Tiger",
    "Toad", "Trout", "Turkey", "Turtle", "Viper",
    "Vulture", "Wallaby", "Walrus", "Wasp", "Weasel",
    "Whale", "Wildcat", "Wolf", "Wolverine", "Wombat",
    "Woodcock", "Woodpecker", "Worm", "Wren", "Yak", "Zebra"
};
const int NUM_ITEMS = sizeof(ITEMS) / sizeof(ITEMS[0]);

typedef struct {
    struct notcurses* nc;
    struct ncplane* main_plane;
    int selected_idx;
    int scroll_offset;
} AppState;

void render_list(AppState* state) {
    if (!state || !state->main_plane) {
        fprintf(stderr, "Render_list: Invalid state or main_plane\n");
        return;
    }

    int plane_rows, plane_cols;
    ncplane_dim_yx(state->main_plane, &plane_rows, &plane_cols); // Returns void
    fprintf(stderr, "Render_list: Plane dimensions: %d rows, %d cols\n", plane_rows, plane_cols);
    if (plane_rows <= 0) {
         fprintf(stderr, "Render_list: Plane has no rows (%d)\n", plane_rows);
         return;
    }

    ncplane_erase(state->main_plane); // Returns void
    // fprintf(stderr, "Render_list: Erased plane (or tried to)\n"); // Optional: confirm action
    
    int displayable_items = plane_rows;
    if (displayable_items > NUM_ITEMS) {
        displayable_items = NUM_ITEMS;
    }
    
    for (int i = 0; i < displayable_items; i++) {
        int item_idx = state->scroll_offset + i;
        if (item_idx >= NUM_ITEMS) break;
        
        if (item_idx == state->selected_idx) {
            if (ncplane_set_fg_rgb(state->main_plane, 0xFFFFFF) < 0) {
                fprintf(stderr, "Render_list: Failed to set_fg_rgb for selected item\n");
            }
            if (ncplane_set_bg_rgb(state->main_plane, 0x0000FF) < 0) {
                fprintf(stderr, "Render_list: Failed to set_bg_rgb for selected item\n");
            }
        }
        
        if (ncplane_putstr_yx(state->main_plane, i, 0, ITEMS[item_idx]) < 0) {
            fprintf(stderr, "Render_list: Failed to putstr_yx for item %d ('%s') at row %d\n", item_idx, ITEMS[item_idx], i);
        }
        
        if (item_idx == state->selected_idx) {
            ncplane_set_fg_default(state->main_plane); // Returns void
            ncplane_set_bg_default(state->main_plane); // Returns void
            // fprintf(stderr, "Render_list: Reset fg/bg for selected item\n"); // Optional
        }
    }
}

int main(void) {
    fprintf(stderr, "Main: Program starting\n");
    if (setlocale(LC_ALL, "") == NULL) {
        fprintf(stderr, "Main: Failed to set locale\n");
        // Continue, but be aware this might cause issues
    }
    AppState app_state = {0};
    app_state.selected_idx = 0;
    app_state.scroll_offset = 0;
    
    struct notcurses_options opts = {0};
    opts.flags = NCOPTION_NO_ALTERNATE_SCREEN | NCOPTION_NO_QUIT_SIGHANDLERS | NCOPTION_SUPPRESS_BANNERS;
    // For more detailed logs from notcurses itself, you could try:
    opts.loglevel = NCLOGLEVEL_TRACE; // or NCLOGLEVEL_DEBUG, NCLOGLEVEL_INFO
    opts.logfp = stderr; // Direct notcurses logs to stderr

    fprintf(stderr, "Main: Initializing notcurses...\n");
    app_state.nc = notcurses_init(&opts, NULL);
    if (!app_state.nc) {
        fprintf(stderr, "Main: notcurses_init failed!\n");
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Main: notcurses_init successful (%p)\n", (void*)app_state.nc);
    
    app_state.main_plane = notcurses_stdplane(app_state.nc);
    if (!app_state.main_plane) {
        fprintf(stderr, "Main: Failed to get standard plane!\n");
        if (notcurses_stop(app_state.nc) < 0) {
            fprintf(stderr, "Main: notcurses_stop failed during error cleanup for stdplane!\n");
        }
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Main: Got main plane (%p)\n", (void*)app_state.main_plane);
    
    fprintf(stderr, "Main: Performing initial render_list call\n");
    render_list(&app_state);
    fprintf(stderr, "Main: Performing initial notcurses_render call\n");
    if (notcurses_render(app_state.nc) < 0) {
        fprintf(stderr, "Main: Initial notcurses_render failed!\n");
        if (notcurses_stop(app_state.nc) < 0) {
            fprintf(stderr, "Main: notcurses_stop failed during error cleanup for initial render!\n");
        }
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Main: Initial render successful. Entering input loop.\n");
    
    ncinput ni;
    uint32_t key_evt;
    while (true) {
        key_evt = notcurses_get_blocking(app_state.nc, &ni);
        if (key_evt == (uint32_t)-1) {
            fprintf(stderr, "Main: notcurses_get_blocking returned an error or EOF. Exiting.\n");
            break;
        }
        // For printable chars, ni.id is the char. key_evt might be the same or an NCKEY_ constant.
        char printable_char = (ni.id < 127 && ni.id > 31) ? (char)ni.id : '.';
        fprintf(stderr, "Main: Got key_evt: 0x%x (ni.id: %u, char: %c, evtype: %d, y: %d, x: %d)\n",
                key_evt, ni.id, printable_char, ni.evtype, ni.y, ni.x);

        bool needs_render = false;
        
        if (ni.evtype == NCTYPE_PRESS && ni.id == 'q') { // Check for 'q' press specifically
             fprintf(stderr, "Main: 'q' pressed, exiting loop.\n");
             break;
        }
        
        switch (key_evt) { // key_evt is often ni.id for simple keys, or special NCKEY_ values
            case NCKEY_UP:
                fprintf(stderr, "Main: NCKEY_UP detected\n");
                if (app_state.selected_idx > 0) {
                    app_state.selected_idx--;
                    if (app_state.selected_idx < app_state.scroll_offset) {
                        app_state.scroll_offset = app_state.selected_idx;
                    }
                    needs_render = true;
                }
                break;
                
            case NCKEY_DOWN:
                fprintf(stderr, "Main: NCKEY_DOWN detected\n");
                if (app_state.selected_idx < NUM_ITEMS - 1) {
                    app_state.selected_idx++;
                    int plane_rows, plane_cols;
                    // It's good practice to check main_plane again, though unlikely to be NULL here if init succeeded
                    if (!app_state.main_plane) {
                         fprintf(stderr, "Main: NCKEY_DOWN: main_plane is NULL unexpectedly!\n");
                        break;
                   }
                   ncplane_dim_yx(app_state.main_plane, &plane_rows, &plane_cols); // Returns void
                   fprintf(stderr, "Main: NCKEY_DOWN: Plane dimensions: %d rows, %d cols\n", plane_rows, plane_cols);
                   if (plane_rows <= 0) {
                         fprintf(stderr, "Main: NCKEY_DOWN: Plane has no rows (%d)\n", plane_rows);
                         break; // Can't calculate scroll correctly
                     }
                    if (app_state.selected_idx >= app_state.scroll_offset + plane_rows) {
                        app_state.scroll_offset = app_state.selected_idx - plane_rows + 1;
                    }
                    needs_render = true;
                }
                break;
                
            case NCKEY_RESIZE:
                fprintf(stderr, "Main: NCKEY_RESIZE detected (y: %d, x: %d)\n", ni.y, ni.x);
                int plane_rows, plane_cols;
                 if (!app_state.main_plane) {
                         fprintf(stderr, "Main: NCKEY_RESIZE: main_plane is NULL unexpectedly!\n");
                        break;
                   }
               ncplane_dim_yx(app_state.main_plane, &plane_rows, &plane_cols); // Returns void
               fprintf(stderr, "Main: NCKEY_RESIZE: New plane dimensions: %d rows, %d cols\n", plane_rows, plane_cols);
               if (plane_rows <= 0) {
                    fprintf(stderr, "Main: NCKEY_RESIZE: Plane has no rows (%d) after resize\n", plane_rows);
                    // Potentially hide or clear content if plane is too small
                }

                // Adjust scroll_offset based on new dimensions
                if (app_state.scroll_offset + plane_rows > NUM_ITEMS) {
                    app_state.scroll_offset = (NUM_ITEMS > plane_rows) ? (NUM_ITEMS - plane_rows) : 0;
                    if (app_state.scroll_offset < 0) app_state.scroll_offset = 0; // Should not happen if NUM_ITEMS > plane_rows logic is correct
                }
                // Ensure selected_idx is still visible
                if (app_state.selected_idx < app_state.scroll_offset) {
                    app_state.scroll_offset = app_state.selected_idx;
                } else if (plane_rows > 0 && app_state.selected_idx >= app_state.scroll_offset + plane_rows) {
                    app_state.scroll_offset = app_state.selected_idx - plane_rows + 1;
                }
                if (app_state.scroll_offset < 0) app_state.scroll_offset = 0; // Final safety

                needs_render = true;
                break;
            // Add other cases as needed, e.g. NCKEY_ENTER for selection
        }
        
        if (needs_render) {
            fprintf(stderr, "Main: needs_render is true. Calling render_list and notcurses_render.\n");
            render_list(&app_state);
            if (notcurses_render(app_state.nc) < 0) {
                fprintf(stderr, "Main: notcurses_render failed in loop!\n");
                // Potentially break or try to recover
            }
        }
    }
    
    fprintf(stderr, "Main: Exited input loop. Stopping notcurses.\n");
    if (notcurses_stop(app_state.nc) < 0) {
        fprintf(stderr, "Main: notcurses_stop failed!\n");
        return EXIT_FAILURE; // Still return failure if stop fails
    }
    fprintf(stderr, "Main: Program finished successfully.\n");
    return EXIT_SUCCESS;
}