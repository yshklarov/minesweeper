#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#ifdef WIN32
// Avoid collision with std::min and std::max
#define NOMINMAX
#include <Windows.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_image.h>

enum side {left, right};
enum luck {neutral, good, bad};

struct cell {
    bool mine;
    bool exploded;
    bool flag;
    bool qmark;
    bool visible;
    bool mistake;
    Uint8 adj;  // Number of adjacent mines
};

// Tries to display value first; if value == nullpointer, then calls and displays value_cb().
struct widget_display1 {
    SDL_Rect pos;
    int *value;
    int (*value_cb)();
};
struct widget_display3 {
    SDL_Rect pos;
    int *value;
    int (*value_cb)();
};
// If tex == nullptr, then calls and displays tex_cb().
struct widget_button {
    SDL_Rect pos;
    SDL_Texture * tex;
    SDL_Texture * (*tex_cb)();
    void (*click_cb)();
    bool depressed;
};

// If the mouse is over the grid, then in_grid == true and the cell coordinates are in (x, y).
struct cursor_grid_location {
    bool in_grid;
    int x, y;
    // Update member data based on the given window coordinates.
    void update(int wx, int wy);
};

static const char* const TITLE{ "Minesweeper" };

static const int MAX_WINDOW_WIDTH {4000};
static const int MAX_WINDOW_HEIGHT {3000};
static const int MAX_FPS {120};
static const int BORDER_WIDTH {5};
static const int TOOLBAR_HEIGHT {40};
static const int DISPLAY1_WIDTH {25};
static const int DISPLAY3_WIDTH {67};
static const double MINE_DENSITY[10] { 0., 0.05, 0.08, 0.10, 0.12, 0.15, 0.18, 0.2, 0.25, 1. };
static const int CELL_DIM[10] { 10, 15, 20, 25, 30, 35, 40, 50, 60, 80 };
static const int GRID_WIDTH[10] { 5, 8, 13, 21, 34, 55, 89, 144, 233, 377 };
static const int GRID_HEIGHT[10] { 3, 5, 8, 13, 21, 34, 55, 89, 144, 233 };

static int cell_dim;
static int grid_width;
static int grid_height;
static int window_width;
static int window_height;
static int grid_side_padding;

static int seconds {0};
SDL_TimerID clock_timer_id;
static int config_density {5};
static int config_gridsize {4};
static int config_zoom {3};
static luck config_luck {neutral};
static bool config_qmarks {false};
enum game_status { game_active, game_won, game_lost };
game_status status { game_lost };
bool first_move { true };
bool grid_depressed{ false };
int visible_cell_count { 0 };
std::vector<widget_button> buttons {};
std::vector<widget_display1> display1s {};
std::vector<widget_display3> display3s {};
cell *minefield;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *window_buffer;

SDL_Texture *(adj[9]);  // adj[0] == empty
SDL_Texture *hidden, *empty, *flag, *qmark, *qmark_off, *mistake, *mine, *boom;
SDL_Texture *face_basic, *face_lost, *face_pending, *face_won;
SDL_Texture *(seg[10]), *seg_off, *seg_minus, *seg_E, *seg_r, *seg_bg_single, *seg_bg_triple;
SDL_Texture *density_less, *density_more, *size_less, *size_more, *zoom_in, *zoom_out;
SDL_Texture *luck_neutral, *luck_good, *luck_bad;

std::random_device rdev {};
std::default_random_engine gen {rdev()};

void register_toolbar_widgets();
void render_all();

void inform(std::string msg) {
#ifdef WIN32
    MessageBox(nullptr, (LPCSTR)msg.c_str(), (LPCSTR)TITLE, MB_ICONINFORMATION);
#else
    std::cerr << msg << std::endl;
#endif
}

void recompute_dimensions() {
    cell_dim = CELL_DIM[config_zoom];
    grid_width = GRID_WIDTH[config_gridsize];
    grid_height = GRID_HEIGHT[config_gridsize];
    int minimum_toolbar_width = 2*(DISPLAY3_WIDTH + 3*DISPLAY1_WIDTH + 4*TOOLBAR_HEIGHT);
    int want_window_width = grid_width * cell_dim + 2*BORDER_WIDTH;
    int min_window_width = minimum_toolbar_width + 2*BORDER_WIDTH;
    window_width = std::max(want_window_width, min_window_width);
    grid_side_padding = std::max(0, min_window_width - want_window_width) / 2;
    window_height = grid_height * cell_dim + TOOLBAR_HEIGHT + 3*BORDER_WIDTH;
}

SDL_Texture *safe_load_texture(const char *filename) {
    SDL_Texture *texture = IMG_LoadTexture(renderer, filename);
    if (texture == nullptr) {
        const std::string msg{ "Failed to load texture" };
        SDL_Quit();
        throw std::runtime_error(msg + " " + filename + ": " + SDL_GetError());
    }
    return texture;
}

void sdl_init_renderer() {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        const std::string msg{ "Failed to create renderer" };
        throw std::runtime_error(msg + ": " + SDL_GetError());
    }
    // We'll render onto a special buffer texture rather than directly onto the display. This way,
    // we can update just a portion of the frame at a time without worrying about double-buffering.
    window_buffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, window_width, window_height);
    SDL_SetRenderTarget(renderer, window_buffer);

#define LOAD_TEXTURE(t) t = safe_load_texture("data/graphics/" #t ".png")
    LOAD_TEXTURE(flag);
    LOAD_TEXTURE(qmark);
    LOAD_TEXTURE(qmark_off);
    LOAD_TEXTURE(hidden);
    LOAD_TEXTURE(mistake);
    LOAD_TEXTURE(mine);
    LOAD_TEXTURE(boom);
    LOAD_TEXTURE(face_basic);
    LOAD_TEXTURE(face_pending);
    LOAD_TEXTURE(face_lost);
    LOAD_TEXTURE(face_won);
    const char *adj_files[]{
        "data/graphics/empty.png",
        "data/graphics/adj1.png",
        "data/graphics/adj2.png",
        "data/graphics/adj3.png",
        "data/graphics/adj4.png",
        "data/graphics/adj5.png",
        "data/graphics/adj6.png",
        "data/graphics/adj7.png",
        "data/graphics/adj8.png"
    };
    for (auto i{ 0 }; i <= 8; ++i) {
        adj[i] = safe_load_texture(adj_files[i]);
    }
    empty = adj[0];
    const char *seg_files[]{
        "data/graphics/seg0.png",
        "data/graphics/seg1.png",
        "data/graphics/seg2.png",
        "data/graphics/seg3.png",
        "data/graphics/seg4.png",
        "data/graphics/seg5.png",
        "data/graphics/seg6.png",
        "data/graphics/seg7.png",
        "data/graphics/seg8.png",
        "data/graphics/seg9.png"
    };
    for (auto i{ 0 }; i <= 9; ++i) {
        seg[i] = safe_load_texture(seg_files[i]);
    }
    LOAD_TEXTURE(seg_off);
    LOAD_TEXTURE(seg_minus);
    LOAD_TEXTURE(seg_E);
    LOAD_TEXTURE(seg_r);
    LOAD_TEXTURE(seg_E);
    LOAD_TEXTURE(seg_bg_single);
    LOAD_TEXTURE(seg_bg_triple);
    LOAD_TEXTURE(density_less);
    LOAD_TEXTURE(density_more);
    LOAD_TEXTURE(size_less);
    LOAD_TEXTURE(size_more);
    LOAD_TEXTURE(zoom_in);
    LOAD_TEXTURE(zoom_out);
    LOAD_TEXTURE(luck_neutral);
    LOAD_TEXTURE(luck_good);
    LOAD_TEXTURE(luck_bad);
#undef LOAD_TEXTURE
}

void sdl_destroy_renderer() {
#define UNLOAD_TEXTURE(t) SDL_DestroyTexture(t)
    UNLOAD_TEXTURE(flag);
    UNLOAD_TEXTURE(qmark);
    UNLOAD_TEXTURE(qmark_off);
    UNLOAD_TEXTURE(hidden);
    UNLOAD_TEXTURE(mistake);
    UNLOAD_TEXTURE(mine);
    UNLOAD_TEXTURE(boom);
    UNLOAD_TEXTURE(face_basic);
    UNLOAD_TEXTURE(face_pending);
    UNLOAD_TEXTURE(face_lost);
    UNLOAD_TEXTURE(face_won);
    for (auto i{ 0 }; i <= 8; ++i) {
        UNLOAD_TEXTURE(adj[i]);
    }
    for (auto i{ 0 }; i <= 9; ++i) {
        UNLOAD_TEXTURE(seg[i]);
    }
    UNLOAD_TEXTURE(seg_off);
    UNLOAD_TEXTURE(seg_minus);
    UNLOAD_TEXTURE(seg_E);
    UNLOAD_TEXTURE(seg_r);
    UNLOAD_TEXTURE(seg_bg_single);
    UNLOAD_TEXTURE(seg_bg_triple);
    UNLOAD_TEXTURE(density_less);
    UNLOAD_TEXTURE(density_more);
    UNLOAD_TEXTURE(size_less);
    UNLOAD_TEXTURE(size_more);
    UNLOAD_TEXTURE(zoom_in);
    UNLOAD_TEXTURE(zoom_out);
    UNLOAD_TEXTURE(luck_neutral);
    UNLOAD_TEXTURE(luck_good);
    UNLOAD_TEXTURE(luck_bad);
    SDL_DestroyRenderer(renderer);
#undef UNLOAD_TEXTURE
}

void sdl_init() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO) != 0) {
        SDL_Quit();
        throw std::runtime_error(std::string("Failed to initialise SDL: ") + SDL_GetError());
    }
    window = SDL_CreateWindow(
        TITLE,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_width,
        window_height,
        0
    );
    if (window == nullptr) {
        const std::string msg{ "Failed to create window" };
        throw std::runtime_error(msg + ": " + SDL_GetError());
    }
    SDL_ShowCursor(SDL_ENABLE);
    SDL_SetWindowResizable(window, SDL_FALSE);
    sdl_init_renderer();
}

void sdl_quit() {
    sdl_destroy_renderer();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void present_window() {
    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderCopy(renderer, window_buffer, NULL, NULL);
    SDL_RenderPresent(renderer);
    SDL_SetRenderTarget(renderer, window_buffer);
}

Uint32 clock_cb(Uint32 interval, void* param) {
    (void)param; // unused
    SDL_Event event;
    SDL_UserEvent userevent;

    userevent.type = SDL_USEREVENT;
    userevent.code = 0;
    userevent.data1 = NULL;
    userevent.data2 = NULL;

    event.type = SDL_USEREVENT;
    event.user = userevent;

    SDL_PushEvent(&event);
    return(interval);
}



void bucket_reveal(int x, int y) {
    if (!minefield[grid_width*y + x].visible) {
        minefield[grid_width*y + x].visible = true;
        ++visible_cell_count;
    }
    minefield[grid_width*y + x].flag = false;
    minefield[grid_width*y + x].qmark = false;
    if (minefield[grid_width*y + x].adj == 0) {
        // TODO Fix stack overflow for very large grids
        for (int j = std::max(y - 1, 0); j <= std::min(y+1, grid_height-1); ++j) {
            for (int i = std::max(x - 1, 0); i <= std::min(x+1, grid_width-1); ++i) {
                if (i == x && j == y) continue;
                if (!minefield[grid_width*j + i].visible) {
                    bucket_reveal(i, j);
                }
            }
        }
    }
}

// Spawn a mine, updating the adjacency data as required.
// Return true if successful; false if there was already a mine there.
bool spawn_mine(int x, int y) {
    if (minefield[x + grid_width * y].mine) {
        return false;
    }
    minefield[grid_width*y + x].mine = true;
    for (int j = std::max(y - 1, 0); j <= std::min(y+1, grid_height-1); ++j) {
        for (int i = std::max(x - 1, 0); i <= std::min(x+1, grid_width-1); ++i ) {
            if (i == x && j == y) continue;
            ++minefield[grid_width*j + i].adj;
        }
    }
    return true;
}

// Remove a mine, updating the adjacency data as required.
// Return true if successful; false if there was no mine there.
bool remove_mine(int x, int y) {
    if (!minefield[x + grid_width * y].mine) {
        return false;
    }
    minefield[grid_width*y + x].mine = false;
    for (int j = std::max(y - 1, 0); j <= std::min(y+1, grid_height-1); ++j) {
        for (int i = std::max(x - 1, 0); i <= std::min(x+1, grid_width-1); ++i ) {
            if (i == x && j == y) continue;
            --minefield[grid_width*j + i].adj;
        }
    }
    for (int j = std::max(y - 1, 0); j <= std::min(y+1, grid_height-1); ++j) {
        for (int i = std::max(x - 1, 0); i <= std::min(x+1, grid_width-1); ++i ) {
            if (i == x && j == y) continue;
            if (minefield[grid_width*j + i].visible) {
                bucket_reveal(i, j);
            }
        }
    }
    return true;
}

void check_for_flag_mistakes() {
    for (int i = 0; i < grid_width * grid_height; ++i) {
        if (minefield[i].flag && !minefield[i].mine) {
           minefield[i].mistake = true;
        }
    }
}

void new_game() {
    delete[] minefield;
    minefield = new cell[grid_width * grid_height];
    visible_cell_count = 0;
    cell virgin{};  // Initialized to 0
    for (int i = 0; i < grid_width * grid_height; ++i) {
        minefield[i] = virgin;
    }

    std::bernoulli_distribution bernoulli(MINE_DENSITY[config_density]);
    for (int y = 0; y < grid_height; ++y) {
        for (int x = 0; x < grid_width; ++x) {
            if (bernoulli(gen)) {
                spawn_mine(x, y);
            }
        }
    }

    status = game_active;
    first_move = true;
    seconds = 0;
    SDL_RemoveTimer(clock_timer_id);  // Okay to remove a non-existent timer
    clock_timer_id = SDL_AddTimer(1000, clock_cb, nullptr);
}



int mines_remaining() {
    if (minefield == nullptr) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < grid_width * grid_height; ++i) {
        if (minefield[i].mine) {
            ++count;
        }
        if (minefield[i].flag && !minefield[i].mistake) {
            --count;
        }
    }
    return count;
}

int mines_total() {
    if (minefield == nullptr) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < grid_width * grid_height; ++i) {
        if (minefield[i].mine) {
            ++count;
        }
    }
    return count;
}

int mines_displayed() {
    return status == game_active ? mines_remaining() : mines_total();
}

// Move the mine at given position to a randomly-selected non-visible mine-free position.
// Return true on success, false if there's no mine at (x,y) or if there's nowhere to move the mine.
bool displace_mine(int x, int y) {
    if (!minefield[x + grid_width * y].mine) {
        return false;
    }
    int available_cells = 0;
    for (int i = 0; i < grid_width * grid_height; ++i) {
        if (!minefield[i].visible && !minefield[i].mine) {
            ++available_cells;
        }
    }
    if (available_cells == 0) {
        return false;
    }
    std::uniform_int_distribution<int> d(1, available_cells);
    int new_location = d(gen);
    for (int j = 0; j < grid_height; ++j) {
        for (int i = 0; i < grid_width; ++i) {
            if (!minefield[i + grid_width*j].visible && !minefield[i + grid_width*j].mine) {
                --new_location;
                if (new_location == 0) {
                    // Must spawn before removing, because remove_mine might make new
                    // cells visible.
                    spawn_mine(i, j);
                    remove_mine(x, y);
                    return true;
                }
            }
        }
    }
    assert(false);
    return false;
}

// Return true if (wx,wy) is inside the minefield grid area.
bool window_coords_in_grid(int wx, int wy) {
    return
        wx >= BORDER_WIDTH &&
        wx < window_width - BORDER_WIDTH &&
        wy >= BORDER_WIDTH*2 + TOOLBAR_HEIGHT &&
        wy < window_height - BORDER_WIDTH;
}

// Return true if (wx,wy) is inside rect.
bool window_coords_in_rect(int wx, int wy, SDL_Rect rect) {
    return
        rect.x <= wx &&
        wx < rect.x + rect.w &&
        rect.y <= wy &&
        wy < rect.y + rect.h;
}

std::tuple<int, int> coords_window_to_grid(int wx, int wy) {
    int x {(wx - BORDER_WIDTH - grid_side_padding) / cell_dim};
    int y {(wy - BORDER_WIDTH*2 - TOOLBAR_HEIGHT) / cell_dim};
    // Clamp to bounds, just in case.
    x = std::min(std::max(0, x), grid_width-1);
    y = std::min(std::max(0, y), grid_height-1);
    return {x, y};
}

void cursor_grid_location::update(int wx, int wy) {
    in_grid = window_coords_in_grid(wx, wy);
    if (in_grid) {
        std::tie(x,y) = coords_window_to_grid(wx, wy);
    } else {
        x = y = 0;
    }
}

void render_clear() {
    // SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black
    SDL_SetRenderDrawColor(renderer, 0xca, 0xca, 0xca, 0xff);  // Border gray
    SDL_RenderClear(renderer);
}

void render_border() {
    // Do nothing for now: render_clear() takes care of it.
    /*
    SDL_SetRenderDrawColor(renderer, 0xca, 0xca, 0xca, 0xff);  // Border gray
    SDL_Rect left { 0, 0, BORDER_WIDTH, window_height };
    SDL_Rect right { window_width - BORDER_WIDTH, 0, BORDER_WIDTH, window_height };
    SDL_Rect top { 0, 0, window_width, BORDER_WIDTH*2 + TOOLBAR_HEIGHT };
    SDL_Rect bottom { 0, window_height - BORDER_WIDTH, window_width, BORDER_WIDTH*2 + TOOLBAR_HEIGHT };
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &right);
    SDL_RenderFillRect(renderer, &top);
    SDL_RenderFillRect(renderer, &bottom);
    */
}

void render_widget_display1 (widget_display1 w) {
    SDL_Texture *digit;
    int value = (w.value != nullptr) ? *w.value : w.value_cb();
    if (0 <= value && value <= 9) {
        digit = seg[value];
    } else {
        // "E" for error
        digit = seg_E;
    }
    SDL_RenderCopy(renderer, seg_bg_single, NULL, &w.pos);
    SDL_RenderCopy(renderer, digit, NULL, &w.pos);
}

void render_widget_display3 (widget_display3 w) {
    int dw, dh, dtw = DISPLAY1_WIDTH, dth = TOOLBAR_HEIGHT;
    double scale = (double)w.pos.h / dth;
    dw = (int)(dtw * scale);
    dh = (int)(dth * scale);
    SDL_Rect digit_2_rect { w.pos.x, w.pos.y, dw, dh };
    SDL_Rect digit_1_rect { w.pos.x - (int)(4*scale) + 1*dw, w.pos.y, dw, dh};
    SDL_Rect digit_0_rect { w.pos.x - (int)(8*scale) + 2*dw, w.pos.y, dw, dh};
    SDL_Texture *digit[3];
    int value = (w.value != nullptr) ? *w.value : w.value_cb();
    if (0 <= value && value <= 999) {
        digit[2] = seg[(value / 100) % 10];
        digit[1] = seg[(value / 10) % 10];
        digit[0] = seg[value % 10];
        if (value <= 99) {
            digit[2] = seg_off;
        }
        if (value <= 9) {
            digit[1] = seg_off;
        }
    } else if (-9 <= value && value <= -1) {
        digit[2] = seg_off;
        digit[1] = seg_minus;
        digit[0] = seg[-value];
    } else if (-99 <= value && value <= -10) {
        digit[2] = seg_minus;
        digit[1] = seg[(- value/10) % 10];
        digit[0] = seg[(- value) % 10];
    } else {
        // "Err" for error
        digit[2] = seg_E;
        digit[1] = digit[0] = seg_r;
    }
    SDL_RenderCopy(renderer, seg_bg_triple, NULL, &w.pos);
    SDL_RenderCopy(renderer, digit[2], NULL, &digit_2_rect);
    SDL_RenderCopy(renderer, digit[1], NULL, &digit_1_rect);
    SDL_RenderCopy(renderer, digit[0], NULL, &digit_0_rect);
}

void render_widget_button(widget_button w) {
    SDL_RenderCopy(renderer, w.depressed ? empty : hidden, NULL, &w.pos);
    SDL_RenderCopy(renderer, w.tex ? w.tex : w.tex_cb(), NULL, &w.pos);
}

// Create a single-digit display together with increment/decrement buttons.
// Return the position data of the entire widget.
SDL_Rect register_num_config_widget(
        int x, int y, SDL_Texture *dec_tex, SDL_Texture *inc_tex,
        void (*dec_cb)(), void (*inc_cb)(), int *value) {
    SDL_Rect inc_rect {x, y, TOOLBAR_HEIGHT/2, TOOLBAR_HEIGHT/2};
    SDL_Rect dec_rect {x, y + TOOLBAR_HEIGHT/2, TOOLBAR_HEIGHT/2, TOOLBAR_HEIGHT/2};
    int tw = DISPLAY1_WIDTH, th = TOOLBAR_HEIGHT, w, h;
    double scale = (double)TOOLBAR_HEIGHT / th;
    w = (int)(tw * scale);
    h = TOOLBAR_HEIGHT;
    SDL_Rect display_rect { x + TOOLBAR_HEIGHT/2, y, w, h };

    widget_button inc_btn { inc_rect, inc_tex, nullptr, inc_cb, false };
    buttons.push_back(inc_btn);
    widget_button dec_btn { dec_rect, dec_tex, nullptr, dec_cb, false };
    buttons.push_back(dec_btn);
    widget_display1 display { display_rect, value, nullptr };
    display1s.push_back(display);

    SDL_Rect widget_rect { x, y, w + TOOLBAR_HEIGHT/2, h };
    return widget_rect;
}

void resize_window(int w, int h) {
    SDL_SetWindowSize(window, w, h);
    SDL_DestroyTexture(window_buffer);
    window_buffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, window_width, window_height);
    SDL_SetRenderTarget(renderer, window_buffer);
}
void decrement_density() {
    if (config_density == 0) return;
    --config_density;
    new_game();
}
void increment_density() {
    if (config_density == 9) return;
    ++config_density;
    new_game();
}
void decrement_gridsize() {
    if (config_gridsize == 0) return;
    --config_gridsize;
    recompute_dimensions();
    resize_window(window_width, window_height);
    register_toolbar_widgets();
    new_game();
    render_all();
}
void increment_gridsize() {
    if (config_gridsize == 9) return;
    ++config_gridsize;
    recompute_dimensions();
    if (window_width >= MAX_WINDOW_WIDTH || window_height >= MAX_WINDOW_HEIGHT) {
        // Rollback
        --config_gridsize;
        recompute_dimensions();
        return;
    }
    resize_window(window_width, window_height);
    register_toolbar_widgets();
    new_game();
    render_all();
}
void decrement_zoom() {
    if (config_zoom == 0) return;
    --config_zoom;
    recompute_dimensions();
    resize_window(window_width, window_height);
    register_toolbar_widgets();
    render_all();
}
void increment_zoom() {
    if (config_zoom == 9) return;
    ++config_zoom;
    recompute_dimensions();
    if (window_width >= MAX_WINDOW_WIDTH || window_height >= MAX_WINDOW_HEIGHT) {
        // Rollback
        --config_zoom;
        recompute_dimensions();
        return;
    }
    resize_window(window_width, window_height);
    register_toolbar_widgets();
    render_all();
}

SDL_Texture* current_face() {
    switch (status) {
    default: [[fallthrough]];
    case game_active:
        return grid_depressed ? face_pending : face_basic;
    case game_won: return face_won;
    case game_lost: return face_lost;
    }
}

void end_game(bool won) {
    if (won) {
        status = game_won;
    }
    else {
        status = game_lost;
    }
    SDL_RemoveTimer(clock_timer_id);  // Stop clock
    check_for_flag_mistakes();
}

void click_face() {
    if (status == game_active && mines_remaining() == 0) {
        bool won {true};
        for (int i = 0; i < grid_width * grid_height; ++i) {
            if (minefield[i].flag != minefield[i].mine) {
                won = false;
                break;
            }
        }
        end_game(won);
    } else if (status == game_active) {
        end_game(false);
    } else {
        new_game();
    }
}

SDL_Texture* current_luck_tex() {
    switch (config_luck) {
    default: [[fallthrough]];
    case neutral: return luck_neutral; break;
    case good: return luck_good; break;
    case bad: return luck_bad; break;
    }
}
void click_luck() {
    switch (config_luck) {
    default: [[fallthrough]];
    case neutral: config_luck = good; break;
    case good: config_luck = bad; break;
    case bad: config_luck = neutral; break;
    }
}
SDL_Texture* current_qmark_tex() {
    return config_qmarks ? qmark : qmark_off;
}
void click_qmark_toggle()  {
    config_qmarks = !config_qmarks;
    if (!config_qmarks) {
       for (int i = 0; i < grid_width * grid_height; ++i) {
            minefield[i].qmark = false;
        }
    }
}

void register_toolbar_widgets() {
    int horiz_padding = TOOLBAR_HEIGHT / 2;

    display3s.clear();
    display1s.clear();
    buttons.clear();

    // Mines remaining
    int tw = DISPLAY3_WIDTH, th = TOOLBAR_HEIGHT, w;
    double scale = (double)TOOLBAR_HEIGHT / th;
    w = (int)(tw * scale);
    SDL_Rect mines_remaining_rect { BORDER_WIDTH, BORDER_WIDTH, w, TOOLBAR_HEIGHT };
    widget_display3 mines_remaining_display {mines_remaining_rect, nullptr, &mines_displayed};
    display3s.push_back(mines_remaining_display);

    // Clock
    SDL_Rect clock_rect { window_width - BORDER_WIDTH - w, BORDER_WIDTH, w, TOOLBAR_HEIGHT };
    widget_display3 clock_display {clock_rect, &seconds, nullptr};
    display3s.push_back(clock_display);

    // Config option: Density
    SDL_Rect rect_density = register_num_config_widget(
        mines_remaining_rect.x + mines_remaining_rect.w + horiz_padding, BORDER_WIDTH,
        density_less, density_more,
        decrement_density, increment_density,
        &config_density);

    // Config option: Grid size
    SDL_Rect rect_gridsize = register_num_config_widget(
        rect_density.x + rect_density.w + horiz_padding, BORDER_WIDTH,
        size_less, size_more,
        decrement_gridsize, increment_gridsize,
        &config_gridsize);

    // Config option: Zoom
    register_num_config_widget(
        rect_gridsize.x + rect_gridsize.w + horiz_padding, BORDER_WIDTH,
        zoom_out, zoom_in,
        decrement_zoom, increment_zoom,
        &config_zoom);

    // Smiley face
    SDL_Rect face_rect {
        (window_width - TOOLBAR_HEIGHT) / 2, BORDER_WIDTH, TOOLBAR_HEIGHT, TOOLBAR_HEIGHT };
    widget_button face_button { face_rect, nullptr, &current_face, &click_face, false };
    buttons.push_back(face_button);

    // Luck toggle
    SDL_Rect luck_rect{
        clock_rect.x - 2*(horiz_padding + TOOLBAR_HEIGHT), BORDER_WIDTH, TOOLBAR_HEIGHT, TOOLBAR_HEIGHT };
    widget_button luck_button { luck_rect, nullptr, &current_luck_tex, &click_luck, false };
    buttons.push_back(luck_button);

    // Question mark toggle
    SDL_Rect qmark_rect{
        clock_rect.x - horiz_padding - TOOLBAR_HEIGHT, BORDER_WIDTH, TOOLBAR_HEIGHT, TOOLBAR_HEIGHT };
    widget_button qmark_button { qmark_rect, nullptr, &current_qmark_tex, &click_qmark_toggle, false };
    buttons.push_back(qmark_button);
}

void render_toolbar() {
    for (auto w : display3s) {
        render_widget_display3(w);
    }
    for (auto w : display1s) {
        render_widget_display1(w);
    }
    for (auto w : buttons) {
        render_widget_button(w);
    }
}

// Render tex in grid position x, y.
void render_cell(int x, int y, SDL_Texture * tex) {
    SDL_Rect rect {   // screen x, screen y, width, height
        cell_dim * x + BORDER_WIDTH + grid_side_padding,
        cell_dim * y + BORDER_WIDTH*2 + TOOLBAR_HEIGHT,
        cell_dim,
        cell_dim};
    SDL_RenderCopy(renderer, tex, NULL, &rect);
}

void render_grid() {
    for (int x = 0; x < grid_width; ++x) {
        for (int y = 0; y < grid_height; ++y) {
            SDL_Rect rect {   // screen x, screen y, width, height
                cell_dim * x + BORDER_WIDTH + grid_side_padding,
                cell_dim * y + BORDER_WIDTH*2 + TOOLBAR_HEIGHT,
                cell_dim,
                cell_dim};
            SDL_Texture* background {minefield[grid_width*y + x].visible ? empty : hidden};
            SDL_RenderCopy(renderer, background, NULL, &rect);

            // Draw revealed cells
            if (minefield[grid_width*y + x].visible) {
                if (minefield[grid_width*y + x].mine) {
                    SDL_RenderCopy(renderer, mine, NULL, &rect);
                } else {
                    assert(0 <= minefield[grid_width * y + x].adj && minefield[grid_width * y + x].adj <= 8);
                    SDL_RenderCopy(renderer, adj[minefield[grid_width * y + x].adj], NULL, &rect);
                }
            }

            // Draw mines
            if (minefield[grid_width * y + x].exploded) {
                SDL_RenderCopy(renderer, boom, NULL, &rect);
            }
            if (status != game_active && minefield[grid_width*y + x].mine && !minefield[grid_width*y + x].flag) {
                SDL_RenderCopy(renderer, mine, NULL, &rect);
            }

            // Draw flags and question marks
            if (minefield[grid_width * y + x].flag) {
                SDL_RenderCopy(renderer, flag, NULL, &rect);
                if (minefield[grid_width * y + x].mistake) {
                    SDL_RenderCopy(renderer, mistake, NULL, &rect);
                }
            }
            if (status == game_active && minefield[grid_width * y + x].qmark) {
                SDL_RenderCopy(renderer, qmark, NULL, &rect);
            }
        }
    }
}

void render_all() {
    render_clear();
    render_border();
    render_toolbar();
    render_grid();
}

void chord_reveal(int x, int y) {


    int flag_count = 0;

    for (int j = std::max(y - 1, 0); j <= std::min(y + 1, grid_height - 1); ++j) {
        for (int i = std::max(x - 1, 0); i <= std::min(x + 1, grid_width - 1); ++i) {
            if (i == x && j == y) continue;
            if (minefield[grid_width * j + i].qmark){
                return;
            }
            if (!minefield[grid_width * j + i].visible && minefield[grid_width * j + i].flag) {
                ++flag_count;
            }
        }
    }

    if (minefield[grid_width * y + x].adj == flag_count) {
        for (int j = std::max(y - 1, 0); j <= std::min(y + 1, grid_height - 1); ++j) {
            for (int i = std::max(x - 1, 0); i <= std::min(x + 1, grid_width - 1); ++i) {
                if (i == x && j == y) continue;
                if (!minefield[grid_width * j + i].visible && !minefield[grid_width * j + i].flag && !minefield[grid_width * j + i].qmark) {
                    if (minefield[grid_width * j + i].mine) {
                        minefield[grid_width * j + i].exploded = true;
                        end_game(false);
                    }
                    else {
                        bucket_reveal(i, j);
                    }
                }
            }
        }
    }
}

void game_main() {
    recompute_dimensions();
    sdl_init();
    register_toolbar_widgets();

    new_game();
    render_all();
    present_window();

    bool quitting{ false };

    int grid_depressed_x, grid_depressed_y;

    int ticks = SDL_GetTicks();
    while (!quitting) {
        SDL_Event evt;
        SDL_SetRenderTarget(renderer, window_buffer);
        if (SDL_WaitEvent(&evt) == 1) {
            if (evt.type == SDL_MOUSEBUTTONDOWN || evt.type == SDL_MOUSEBUTTONUP || evt.type == SDL_MOUSEMOTION) {
                if (window_coords_in_grid(evt.button.x, evt.button.y) && status == game_active) {  // Mouse in grid
                    auto [x, y] = coords_window_to_grid(evt.button.x, evt.button.y);
                    if (evt.type == SDL_MOUSEBUTTONDOWN
                            && evt.button.button == SDL_BUTTON_RIGHT
                            && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) == 0) {
                       // Toggle flag (and question mark) if this cell is hidden.
                        if (!minefield[grid_width*y + x].visible) {
                            if (minefield[grid_width * y + x].flag) {
                                minefield[grid_width * y + x].flag = false;
                                if (config_qmarks) {
                                     minefield[grid_width * y + x].qmark = true;
                                }
                            } else if (minefield[grid_width * y + x].qmark) {
                              minefield[grid_width * y + x].qmark = false;
                            } else {
                                 minefield[grid_width*y + x].flag = true;
                            }
                            render_toolbar();  // To update mine counter
                            render_grid();
                        }
                    }
                    else if (evt.type == SDL_MOUSEBUTTONDOWN && evt.button.button == SDL_BUTTON_LEFT) {
                        // Handle ordinary mouse down in grid.
                        if (grid_depressed) {
                            // Un-depress the previously-depressed cell
                            render_cell(grid_depressed_x, grid_depressed_y, hidden);
                            grid_depressed = false;
                            render_toolbar();  // Update smiley face
                        }
                        if (!minefield[grid_width * y + x].visible
                            && !minefield[grid_width * y + x].flag
                            && !minefield[grid_width * y + x].qmark) {
                            render_cell(x, y, empty);
                            if (!grid_depressed) {
                                grid_depressed = true;
                                render_toolbar();  // Update smiley face
                            }
                            grid_depressed_x = x;
                            grid_depressed_y = y;
                        }
                    }
                    else if (evt.type == SDL_MOUSEBUTTONDOWN && evt.button.button == SDL_BUTTON_MIDDLE) {
                        for (int j = std::max(y - 1, 0); j <= std::min(y + 1, grid_height - 1); ++j) {
                            for (int i = std::max(x - 1, 0); i <= std::min(x + 1, grid_width - 1); ++i) {
                                if (!minefield[grid_width * j + i].visible && !minefield[grid_width * j + i].flag && !minefield[grid_width * j + i].qmark) {
                                    render_cell(i, j, empty);
                                }
                            }
                        }
                        grid_depressed_x = x;
                        grid_depressed_y = y;
                        grid_depressed = true;
                        render_toolbar();  // Update smiley face

                    } else if (evt.type == SDL_MOUSEMOTION && evt.motion.state & SDL_BUTTON_MMASK){
                        if (grid_depressed) {
                            // Un-depress the previously-depressed cell
                            for (int j = std::max(grid_depressed_y - 1, 0); j <= std::min(grid_depressed_y + 1, grid_height - 1); ++j) {
                                for (int i = std::max(grid_depressed_x - 1, 0); i <= std::min(grid_depressed_x + 1, grid_width - 1); ++i) {
                                    if (!minefield[grid_width * j + i].visible && !minefield[grid_width * j + i].flag && !minefield[grid_width * j + i].qmark) {
                                        render_cell(i, j, hidden);
                                    }
                                }
                            }
                            grid_depressed = false;
                            render_toolbar();  // Update smiley face
                        }
                        if (!minefield[grid_width * y + x].flag
                            && !minefield[grid_width * y + x].qmark) {

                            for (int j = std::max(y - 1, 0); j <= std::min(y + 1, grid_height - 1); ++j) {
                                for (int i = std::max(x - 1, 0); i <= std::min(x + 1, grid_width - 1); ++i) {
                                    if (!minefield[grid_width * j + i].visible && !minefield[grid_width * j + i].flag && !minefield[grid_width * j + i].qmark) {
                                        render_cell(i, j, empty);
                                    }
                                }
                            }

                            if (!grid_depressed) {
                                grid_depressed = true;
                                render_toolbar();  // Update smiley face
                            }
                            grid_depressed_x = x;
                            grid_depressed_y = y;
                        }

                    } else if (evt.type == SDL_MOUSEBUTTONUP && evt.button.button == SDL_BUTTON_MIDDLE) {
                        if (minefield[grid_width * y + x].visible) {
                            chord_reveal(x, y);
                        }
                        if (visible_cell_count + mines_total() == grid_width * grid_height) {
                            // All non-mine cells revealed.
                            status = game_won;
                            SDL_RemoveTimer(clock_timer_id);  // Stop clock
                        }
                         grid_depressed = false;
                         render_grid();
                         render_toolbar(); // For game-over face

                    } else if (evt.type == SDL_MOUSEBUTTONUP && evt.button.button == SDL_BUTTON_LEFT) {
                        // Handle ordinary mouse up in grid.
                        if (evt.button.button == SDL_BUTTON_LEFT
                              && !minefield[grid_width*y + x].flag
                              && !minefield[grid_width*y + x].qmark) {
                            bool reveal {false};
                            if (minefield[grid_width*y + x].mine) {
                                if ((first_move || config_luck == good) && displace_mine(x,y)) {
                                    reveal = true;
                                } else {  // Neither first move nor lucky, or displace_mine() failed
                                    minefield[grid_width*y + x].exploded = true;
                                    end_game(false);
                                    render_toolbar();  // For game-over face
                                }
                            } else {  // No mine here
                                // TODO deal with bad luck
                                reveal = true;
                            }
                            if (reveal) {
                                bucket_reveal(x, y);
                                if (visible_cell_count + mines_total() == grid_width * grid_height) {
                                     // All non-mine cells revealed.
                                     status = game_won;
                                     SDL_RemoveTimer(clock_timer_id);  // Stop clock
                                }
                             }
                            first_move = false;
                        }
                        grid_depressed = false;
                        render_toolbar();  // Update smiley face
                        render_grid();
                    } else if (evt.type == SDL_MOUSEMOTION && evt.motion.state & SDL_BUTTON_LMASK) {
                        // Handle mouse motion in grid
                        if (grid_depressed) {
                            // Un-depress the previously-depressed cell
                            render_cell(grid_depressed_x, grid_depressed_y, hidden);
                            grid_depressed = false;
                            render_toolbar();  // Update smiley face
                        }
                        if (!minefield[grid_width * y + x].visible
                              && !minefield[grid_width*y + x].flag
                              && !minefield[grid_width*y + x].qmark) {
                            render_cell(x, y, empty);
                            if (!grid_depressed) {
                                grid_depressed = true;
                                render_toolbar();  // Update smiley face
                            }
                            grid_depressed_x = x;
                            grid_depressed_y = y;
                        }
                    }
                } else { // Mouse not in grid
                    bool toolbar_updated = false;
                    bool grid_updated = false;
                    if (grid_depressed) {
                        // Un-depress any previously-depressed cells
                        for (int j = std::max(grid_depressed_y - 1, 0); j <= std::min(grid_depressed_y + 1, grid_height - 1); ++j) {
                            for (int i = std::max(grid_depressed_x - 1, 0); i <= std::min(grid_depressed_x + 1, grid_width - 1); ++i) {
                                if (!minefield[grid_width * j + i].visible && !minefield[grid_width * j + i].flag && !minefield[grid_width * j + i].qmark) {
                                    render_cell(i, j, hidden);
                                }
                            }
                        }
                        grid_depressed = false;
                        toolbar_updated = true;  // Update smiley face
                    }
                    if (evt.type == SDL_MOUSEBUTTONDOWN && evt.button.button == SDL_BUTTON_LEFT) {
                        for (auto & b : buttons) {
                            if (window_coords_in_rect(evt.button.x, evt.button.y, b.pos)) {
                                b.depressed = true;
                                toolbar_updated = true;
                            }
                        }
                    } else if (evt.type == SDL_MOUSEMOTION) {
                        for (auto & b : buttons) {
                            if (!window_coords_in_rect(evt.button.x, evt.button.y, b.pos)) {
                                if (b.depressed) {
                                    b.depressed = false;
                                    toolbar_updated = true;
                                }
                            }
                        }
                    } else if (evt.type == SDL_MOUSEBUTTONUP && evt.button.button == SDL_BUTTON_LEFT) {
                        for (auto & b : buttons) {
                            if (window_coords_in_rect(evt.button.x, evt.button.y, b.pos) && b.depressed) {
                                b.depressed = false;
                                toolbar_updated = true;
                                b.click_cb();
                                grid_updated = true;  // Some buttons change the grid.
                            }
                        }
                    }
                    if (toolbar_updated) {
                        render_toolbar();
                    }
                    if (grid_updated) {
                        render_grid();
                    }
                }
            } else if (evt.type == SDL_QUIT) {
                quitting = true;
            } else if (evt.type == SDL_USEREVENT) {
                ++seconds;
                render_toolbar();
            }
        }
        else {
            SDL_Quit();
            const std::string msg{ "SDL Event error: " };
            throw std::runtime_error(msg + SDL_GetError());
        }

        // This is a weird way to cap FPS: better to process all events, first. Oh, well.
        int ticks_now = SDL_GetTicks();
        int delta = ticks_now - ticks;
        bool no_further_events = SDL_WaitEventTimeout(nullptr, 0) == 0;
        if (delta > 1000.0/MAX_FPS || no_further_events) {
          present_window();
          ticks = ticks_now;
        }
    }
    delete[] minefield;
    sdl_quit();
}


#ifdef WIN32
int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
        ) {
    try {
        game_main();
    }
    catch (std::exception& e) {
        using namespace std::string_literals;
        std::string msg{
            "Caught exception at top level:\n\n"s +
            std::string(e.what()) };
        std::string title{ std::string(TITLE) + ": Critical Error"s };
        MessageBox(
            nullptr,
            (LPCSTR)msg.c_str(),
            (LPCSTR)title.c_str(),
            MB_ICONERROR);
        return -1;
    }
    return 0;
}

#else
int main(int argc, char* argv[]) {
    (void)argv; // unused
    switch (argc) {
    case 1:
        break;
    default:
        std::cerr << "Too many command-line arguments.\n";
        return -1;
    }

    try {
        game_main();
    } catch (std::exception &e) {
        std::cerr << "Caught exception at top level: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
#endif
