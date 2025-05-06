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
    int plane_rows, plane_cols;
    ncplane_dim_yx(state->main_plane, &plane_rows, &plane_cols);
    
    ncplane_erase(state->main_plane);
    
    int displayable_items = plane_rows;
    if (displayable_items > NUM_ITEMS) {
        displayable_items = NUM_ITEMS;
    }
    
    for (int i = 0; i < displayable_items; i++) {
        int item_idx = state->scroll_offset + i;
        if (item_idx >= NUM_ITEMS) break;
        
        if (item_idx == state->selected_idx) {
            ncplane_set_fg_rgb(state->main_plane, 0xFFFFFF);
            ncplane_set_bg_rgb(state->main_plane, 0x0000FF);
        }
        
        ncplane_putstr_yx(state->main_plane, i, 0, ITEMS[item_idx]);
        
        if (item_idx == state->selected_idx) {
            ncplane_set_fg_default(state->main_plane);
            ncplane_set_bg_default(state->main_plane);
        }
    }
}

int main(void) {
    AppState app_state = {0};
    app_state.selected_idx = 0;
    app_state.scroll_offset = 0;
    
    struct notcurses_options opts = {0};
    app_state.nc = notcurses_init(&opts, NULL);
    if (!app_state.nc) {
        return EXIT_FAILURE;
    }
    
    app_state.main_plane = notcurses_stdplane(app_state.nc);
    
    render_list(&app_state);
    notcurses_render(app_state.nc);
    
    ncinput ni;
    uint32_t key_evt;
    while ((key_evt = notcurses_get_blocking(app_state.nc, &ni)) != (uint32_t)-1) {
        bool needs_render = false;
        
        if (key_evt == 'q') break;
        
        switch (key_evt) {
            case NCKEY_UP:
                if (app_state.selected_idx > 0) {
                    app_state.selected_idx--;
                    if (app_state.selected_idx < app_state.scroll_offset) {
                        app_state.scroll_offset = app_state.selected_idx;
                    }
                    needs_render = true;
                }
                break;
                
            case NCKEY_DOWN:
                if (app_state.selected_idx < NUM_ITEMS - 1) {
                    app_state.selected_idx++;
                    int plane_rows, plane_cols;
                    ncplane_dim_yx(app_state.main_plane, &plane_rows, &plane_cols);
                    if (app_state.selected_idx >= app_state.scroll_offset + plane_rows) {
                        app_state.scroll_offset = app_state.selected_idx - plane_rows + 1;
                    }
                    needs_render = true;
                }
                break;
                
            case NCKEY_RESIZE:
                int plane_rows, plane_cols;
                ncplane_dim_yx(app_state.main_plane, &plane_rows, &plane_cols);
                if (app_state.scroll_offset + plane_rows > NUM_ITEMS) {
                    app_state.scroll_offset = (NUM_ITEMS > plane_rows) ? (NUM_ITEMS - plane_rows) : 0;
                    if (app_state.scroll_offset < 0) app_state.scroll_offset = 0;
                }
                needs_render = true;
                break;
        }
        
        if (needs_render) {
            render_list(&app_state);
            notcurses_render(app_state.nc);
        }
    }
    
    notcurses_stop(app_state.nc);
    return EXIT_SUCCESS;
}