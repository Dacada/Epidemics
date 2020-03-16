/*
 * An extremely simple program to simulate an epidemic, producing
 * pretty graphics and colors.
 */


////////////////////////
/////[PREPROCESSOR]/////
////////////////////////

#include <allegro5/allegro5.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>
#include <argp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

#define DISPLAYX 750
#define DISPLAYY 500

#define CURED_STATE (-128)
#define DEAD_STATE (-256)

#define MAX(a,b) ((a) > (b) ? (a) : (b))


///////////////////////////
/////[DATA STRUCTURES]/////
///////////////////////////

struct settings {
        ALLEGRO_COLOR background_color, text_color, ui_color;
        ALLEGRO_COLOR healthy_color, cured_color, dead_color;
        ALLEGRO_COLOR infected_color_min, infected_color_max;
        ALLEGRO_FONT *text_font;
        int simulation_grid_dimension;
        int max_infected_value;
        double simulation_timestep, lethality, infectiousness;
        double immunization_chance;
        int steplimit;
        bool step_at_a_time;
        int rng_seed;
};


///////////////////
/////[GLOBALS]/////
///////////////////

// For argp
const char *argp_program_version = "epidemics 1.0";
const char *argp_program_bug_address = "<mail@davidcarreracasado.cat>";


////////////////////////////
/////[ARGUMENT PARSING]/////
////////////////////////////

static double parse_double(char *str, struct argp_state *state) {
        char *next;
        double ret;

        errno = 0;
        ret = strtod(str, &next);
        
        if (errno == 0 && ret == 0 && next == str) {
                argp_error(state, "failed to parse as double: %s", str);
        } else if (errno != 0) {
                argp_error(state, "%s: %s", strerror(errno), str);
        }

        return ret;
}

static int parse_int(char *str, bool allow_negative, struct argp_state *state) {
        char *next;
        long ret;

        errno = 0;
        ret = strtol(str, &next, 0);
        
        if (errno == 0 && ret == 0 && next == str) {
                argp_error(state, "failed to parse as integer: %s", str);
        } else if (errno != 0) {
                argp_error(state, "%s: %s", strerror(errno), str);
        } else if (!allow_negative && ret < 0) {
                argp_error(state, "invalid negative integer: %s", str);
        } else if (ret > INT_MAX || ret < INT_MIN) {
                argp_error(state, "%s: %s", strerror(ERANGE), str);
        }

        return (int)ret;
}

static unsigned char parse_rgb_component(char *str, char **next, struct argp_state *state, char *base) {
        errno = 0;
        long r = strtol(str, next, 0);

        if (errno == 0 && r == 0 && *next == str) {
                argp_error(state, "failed to parse r component of: %s", base);
        } else if (errno != 0) {
                argp_error(state, "%s: %s", strerror(errno), base);
        } else if (r < 0) {
                argp_error(state, "invalid negative r component of: %s", base);
        } else if (r > UCHAR_MAX) {
                argp_error(state, "%s: %s", strerror(ERANGE), base);
        }

        return (unsigned char)r;
}

static ALLEGRO_COLOR parse_rgb(char *str, struct argp_state *state) {
        char *prev = str;
        char *next;
        
        unsigned char r = parse_rgb_component(prev, &next, state, str);
        prev = next+1;
        
        unsigned char g = parse_rgb_component(prev, &next, state, str);
        prev = next+1;
        
        unsigned char b = parse_rgb_component(prev, &next, state, str);

        return al_map_rgb(r, g, b);
}

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
        struct settings *settings = state->input;

        switch(key) {
        case 's':
                settings->step_at_a_time = true;
                break;
        case 'l':
                settings->lethality = parse_double(arg, state);
                break;
        case 'i':
                settings->infectiousness = parse_double(arg, state);
                break;
        case 'm':
                settings->max_infected_value = parse_int(arg, false, state);
                break;
        case 'c':
                settings->immunization_chance = parse_double(arg, state);
                break;
        case 'd':
                settings->simulation_grid_dimension = parse_int(arg, false, state);
                break;
        case 't':
                settings->simulation_timestep = parse_double(arg, state);
                break;
        case 'p':
                settings->steplimit = parse_int(arg, false, state);
                break;
        case 'r':
                settings->rng_seed = parse_int(arg, true, state);
                break;
        case 40001:
                settings->healthy_color = parse_rgb(arg, state);
                break;
        case 40002:
                settings->cured_color = parse_rgb(arg, state);
                break;
        case 40003:
                settings->dead_color = parse_rgb(arg, state);
                break;
        case 40004:
                settings->infected_color_max = parse_rgb(arg, state);
                break;
        case 40005:
                settings->infected_color_min = parse_rgb(arg, state);
                break;
        case 50001:
                settings->background_color = parse_rgb(arg, state);
                break;
        case 50002:
                settings->text_color = parse_rgb(arg, state);
                break;
        case 50003:
                settings->ui_color = parse_rgb(arg, state);
                break;
        default:
                return ARGP_ERR_UNKNOWN;
        }

        return 0;
}

static void parse_args(int argc, char *argv[], struct settings *settings) {
        static struct argp_option options[] = {
                {
                        .name="manual-step",
                        .key='s',
                        .arg=NULL,
                        .flags=0,
                        .doc="If set, a simulation step will run only when pressing space.",
                        .group=1,
                },
                
                {
                        .name="lethality",
                        .key='l',
                        .arg="value",
                        .flags=0,
                        .doc="Probability of death of an infected individual on each simulation "
                        "step. Defaults to 0.01",
                        .group=2,
                },
                {
                        .name="infectiousness",
                        .key='i',
                        .arg="value",
                        .flags=0,
                        .doc="Probability that a healthy individual becomes infected on each "
                        "simulation step, if there's an infected individual next to "
                        "it. Defaults to 0.1",
                        .group=2,
                },
                {
                        .name="immunity",
                        .key='m',
                        .arg="value",
                        .flags=0,
                        .doc="After this many steps, an infected individual can be cured. Defaults "
                        "to 10",
                        .group=2,
                },
                {
                        .name="immunization",
                        .key='c',
                        .arg="value",
                        .flags=0,
                        .doc="Probability of an individual that has been infected for 'immunity' "
                        "steps to be cured. Defaults to 1.0",
                        .group=2,
                },
                {
                        .name="dimension",
                        .key='d',
                        .arg="value",
                        .flags=0,
                        .doc="Dimension of the simulated square of individuals as a single positive"
                        "integer. Defaults to 100 meaning a square of 100x100 individuals.",
                        .group=2,
                },
                
                {
                        .name="timestep",
                        .key='t',
                        .arg="value",
                        .flags=0,
                        .doc="Seconds between simulation steps. Ignored in manual step "
                        "mode. Defaults to 0.1",
                        .group=3,
                },
                {
                        .name="step-limit",
                        .key='p',
                        .arg="value",
                        .flags=0,
                        .doc="Total history of steps kept by graphic. After this many steps have "
                        "passed, the plot stops scrolling. Default is 4096 and it's more than "
                        "enough.",
                        .group=3,
                },
                {
                        .name="seed",
                        .key='r',
                        .arg="value",
                        .flags=0,
                        .doc="Seed to use for the RNG (srand). Default is to use the value of "
                        "time(NULL).",
                        .group=3,
                },

                {
                        .name="color-healthy",
                        .key=40001,
                        .arg="r,g,b",
                        .flags=0,
                        .doc="Color to use to represent 'healthiness'. Default is 0,255,0",
                        .group=4,
                },
                {
                        .name="color-cured",
                        .key=40002,
                        .arg="r,g,b",
                        .flags=0,
                        .doc="Color to use to represent 'curedness'. Default is 255,255,0",
                        .group=4,
                },
                {
                        .name="color-dead",
                        .key=40003,
                        .arg="r,g,b",
                        .flags=0,
                        .doc="Color to use to represent 'deadness'. Default is 255,0,255",
                        .group=4,
                },
                {
                        .name="color-infected-max",
                        .key=40004,
                        .arg="r,g,b",
                        .flags=0,
                        .doc="Starting range for color to use to represent 'infectedness'. Infected "
                        "individuals will linearly range from this color to the min version "
                        "depending on how many steps they've spent infected. This is the color "
                        "of individuals who've spent the most time infected, and also used for "
                        "general 'infectedness' in UI. Default is 255,0,0",
                        .group=4,
                },
                {
                        .name="color-infected-min",
                        .key=40005,
                        .arg="r,g,b",
                        .flags=0,
                        .doc="Ending range for color to use to represent 'infectedness'. Infected "
                        "individuals will linearly range from this color to the max version "
                        "depending on how many steps they've spent infected. This is the color "
                        "of individuals who've spent the least time infected, and it isn't used "
                        "in the UI at all. Default is 128,0,0",
                        .group=4,
                },
                
                {
                        .name="color-background",
                        .key=50001,
                        .arg="r,g,b",
                        .flags=0,
                        .doc="Color to use for the background. Default is 0,0,0",
                        .group=5,
                },
                {
                        .name="color-text",
                        .key=50002,
                        .arg="r,g,b",
                        .flags=0,
                        .doc="Color to use for the general text. Default is 255,255,255",
                        .group=5,
                },
                {
                        .name="color-ui",
                        .key=50003,
                        .arg="r,g,b",
                        .flags=0,
                        .doc="Color to use for the ui elements. Default is 255,255,255",
                        .group=5,
                },
                
                {0},
        };
        static char doc[] = "A simple simulation of an epidemic with pretty colors and "
                "graphics.\vPressing ESC exits the simulation as well as just closing "
                "the window. Space can either advance a step in the simulation or "
                "pause/unpause it, depending on whether manual step is enabled. For "
                "options taking integers as arguments, these are parsed correctly as "
                "hexadecimal if starting with 0x, octal if otherwise starting with 0 "
                "and decimal in any other case. The same holds for rgb components.";
        static struct argp arg = {
                .options = options,
                .parser = parse_opt,
                .args_doc = NULL,
                .doc = doc,
                .children = NULL,
                .help_filter = NULL,
                .argp_domain = NULL
        };

        // First load defaults
        settings->step_at_a_time = false;
        
        settings->lethality = 0.01;
        settings->infectiousness = 0.1;
        settings->max_infected_value = 10;
        settings->immunization_chance = 1.0;
        settings->simulation_grid_dimension = 100;
        
        settings->simulation_timestep = 0.1;
        settings->steplimit = 1<<12;
        settings->rng_seed = time(NULL);
        
        settings->healthy_color = al_map_rgb(0x00, 0xFF, 0x00);
        settings->cured_color = al_map_rgb(0xFF, 0xFF, 0x00);
        settings->dead_color = al_map_rgb(0xFF, 0x00, 0xFF);
        settings->infected_color_min = al_map_rgb(0x80, 0x00, 0x00);
        settings->infected_color_max = al_map_rgb(0xFF, 0x00, 0x00);
        
        settings->background_color = al_map_rgb(0x00, 0x00, 0x00);
        settings->text_color = al_map_rgb(0xff, 0xff, 0xff);
        settings->ui_color = al_map_rgb(0xff, 0xff, 0xff);

        // Now load from arguments
        argp_parse(&arg, argc, argv, 0, NULL, settings);
}


/////////////////////////////
/////[UTILITY FUNCTIONS]/////
/////////////////////////////

static void must_init(bool test, const char *description) {
        if(test) {
                return;
        }
        
        fprintf(stderr, "couldn't initialize %s\n", description);
        exit(1);
}

static void tally_state(const int *state, int dim, int *healthy, int *infected, int *cured, int *dead) {
        *healthy = *infected = *cured = *dead = 0;
        
        for (int i=0; i<dim; i++) {
                for (int j=0; j<dim; j++) {
                        int s = state[j+i*dim];
                        if (s == CURED_STATE) {
                                (*cured)++;
                        } else if (s == DEAD_STATE) {
                                (*dead)++;
                        } else if (s == 0) {
                                (*healthy)++;
                        } else {
                                (*infected)++;
                        }
                }
        }
}

/*
 * Randomly return true with a given probability.
 */
static bool chance(double probability) {
        return (double)rand() / (double)RAND_MAX < probability;
}

static double interpolate(double min, double max, int maxval, int currval) {
        return min+(((max - min)/maxval)*currval);
}


////////////////////////////////
/////[SIMULATION FUNCTIONS]/////
////////////////////////////////

static void init_simulation(struct settings *settings, int *state) {
        memset(state, 0, sizeof(int)*settings->simulation_grid_dimension*settings->simulation_grid_dimension);
        int mid = settings->simulation_grid_dimension/2;
        state[mid+mid*settings->simulation_grid_dimension] = 1;
}

static bool isinfected(const int *state, int x, int y, int size) {
        return state[y+x*size] > 0;
}

static void advance_state(const int *current, int *next, struct settings *settings, int x, int y, int size) {
        int index = y + x * size;
        if (current[index] == CURED_STATE || current[index] == DEAD_STATE) {
                next[index] = current[index];
        } else if (current[index] != 0) {
                if (chance(settings->lethality)) {
                        next[index] = DEAD_STATE;
                } else {
                        next[index] = current[index]+1;
                        if (next[index] > settings->max_infected_value) {
                                if (chance(settings->immunization_chance)) {
                                        next[index] = CURED_STATE;
                                } else {
                                        next[index] = settings->max_infected_value;
                                }
                        }
                }
        } else {
                if ((x > 0      && isinfected(current, x-1, y, size)) ||
                    (x < size-1 && isinfected(current, x+1, y, size)) ||
                    (y > 0      && isinfected(current, x, y-1, size)) ||
                    (y < size-1 && isinfected(current, x, y+1, size))) {
                        if (chance(settings->infectiousness)) {
                                next[index] = 1;
                        } else {
                                next[index] = 0;
                        }
                } else {
                        next[index] = 0;
                }
        }
}

static int *simulation_step(struct settings *settings, int *state) {
        size_t size = sizeof(int) * settings->simulation_grid_dimension * settings->simulation_grid_dimension;
        int *next_state = malloc(size);
        
        for (int i=0; i<settings->simulation_grid_dimension; i++) {
                for (int j=0; j<settings->simulation_grid_dimension; j++) {
                        advance_state(state, next_state, settings, i, j, settings->simulation_grid_dimension);
                }
        }

        free(state);
        return next_state;
}


////////////////////////
/////[UI FUNCTIONS]/////
////////////////////////

static ALLEGRO_COLOR get_cell_color(struct settings *settings, int state) {
        if (state == 0) {
                return settings->healthy_color;
        } else if (state == CURED_STATE) {
                return settings->cured_color;
        } else if (state == DEAD_STATE) {
                return settings->dead_color;
        } else if (state > 0) {
                ALLEGRO_COLOR color;
                color.r = interpolate(settings->infected_color_min.r,
                                      settings->infected_color_max.r,
                                      settings->max_infected_value,
                                      state);
                color.g = interpolate(settings->infected_color_min.g,
                                      settings->infected_color_max.g,
                                      settings->max_infected_value,
                                      state);
                color.b = interpolate(settings->infected_color_min.b,
                                      settings->infected_color_max.b,
                                      settings->max_infected_value,
                                      state);
                color.a = interpolate(settings->infected_color_min.a,
                                      settings->infected_color_max.a,
                                      settings->max_infected_value,
                                      state);
                return color;
        } else {
                return al_map_rgb(0,0,0); // ERROR
        }
}

static void draw_ui_rectangle(int offx, int offy, struct settings *settings, int *state) {
        int size = DISPLAYY / settings->simulation_grid_dimension;
        
        for (int i=0; i<settings->simulation_grid_dimension; i++) {
                for (int j=0; j<settings->simulation_grid_dimension; j++) {
                        ALLEGRO_COLOR color = get_cell_color(settings,
                                                             state[j+i*settings->simulation_grid_dimension]);
                        
                        int x = offx + i * size;
                        int y = offy + j * size;
                        int xx = offx + x + size;
                        int yy = offy + y + size;
                        al_draw_filled_rectangle(x, y, xx, yy, color);
                }
        }
}

__attribute__((format (printf, 7, 8)))
static void draw_ui_panel_text(ALLEGRO_FONT *font,
                                      ALLEGRO_COLOR color,
                                      int x1, int x2, int y,
                                      const char *title, const char *fmt, ...) {
        static char value[256];
        va_list ap;
        
        va_start(ap, fmt);
        vsnprintf(value, 256, fmt, ap);
        va_end(ap);
        
        al_draw_text(font, color, x1, y, 0, title);
        al_draw_text(font, color, x2, y, ALLEGRO_ALIGN_RIGHT, value);
}

static void plot_graph(int offx, int offy, int width, int height,
                       int healthy, int infected, int cured, int dead,
                       struct settings *settings, bool step) {
        static int len = 0;
        static int *hist[4] = {NULL,NULL,NULL,NULL};
        int curr[4] = {healthy,infected,cured,dead};
        ALLEGRO_COLOR colors[4] = {settings->healthy_color, settings->infected_color_max,
                                   settings->cured_color, settings->dead_color};
        int total = healthy+infected+cured+dead;

        bool grow = len < settings->steplimit && step;

        if (grow) {
                len++;
        }
        
        for (int i=0; i<4; i++) {
                if (hist[i] == NULL) {
                        hist[i] = malloc(sizeof(int) * settings->steplimit);
                }
                
                if (grow) {
                        hist[i][len-1] = curr[i];
                }
                
                for (int j=0; j<len; j++) {
                        if (j >= len - width) {
                                int x = offx + j - MAX(0, len-width);
                                int y = offy + height - hist[i][j]*height/total;
                                al_draw_pixel(x, y, colors[i]);
                        }
                }
        }
}

static void draw_ui_panel(int offx, int offy, int width, int height,
                          struct settings *settings, int *state, bool step) {
        al_draw_line(offx+0, offy+0,
                     offx+0, offy+DISPLAYY,
                     settings->ui_color, 4);

        int x1=30;
        int x2=220;
        int y=10;
        
        draw_ui_panel_text(settings->text_font, settings->text_color,
                           offx+x1, offx+x2, offy+(y+=10),
                           "Lethality:", "%f", settings->lethality);
        
        draw_ui_panel_text(settings->text_font, settings->text_color,
                           offx+x1, offx+x2, offy+(y+=10),
                           "Infectiousness:", "%f", settings->infectiousness);
        
        draw_ui_panel_text(settings->text_font, settings->text_color,
                           offx+x1, offx+x2, offy+(y+=10),
                           "Immunity:", "%d", settings->max_infected_value);
        
        draw_ui_panel_text(settings->text_font, settings->text_color,
                           offx+x1, offx+x2, offy+(y+=10),
                           "Immunization:", "%f", settings->immunization_chance);

        y += 30;

        al_draw_line(offx+0, offy+y,
                     offx+width, offy+y,
                     settings->ui_color, 4);

        y += 10;

        int healthy, infected, cured, dead;
        tally_state(state, settings->simulation_grid_dimension, &healthy, &infected, &cured, &dead);
        
        draw_ui_panel_text(settings->text_font, settings->healthy_color,
                           offx+x1, offx+x2, offy+(y+=10),
                           "Healthy:", "%d", healthy);
        draw_ui_panel_text(settings->text_font, settings->infected_color_max,
                           offx+x1, offx+x2, offy+(y+=10),
                           "Infected:", "%d", infected);
        draw_ui_panel_text(settings->text_font, settings->cured_color,
                           offx+x1, offx+x2, offy+(y+=10),
                           "Cured:", "%d", cured);
        draw_ui_panel_text(settings->text_font, settings->dead_color,
                           offx+x1, offx+x2, offy+(y+=10),
                           "Dead:", "%d", dead);

        y += 30;

        al_draw_line(offx+0, offy+y,
                     offx+width, offy+y,
                     settings->ui_color, 4);

        y += 30;

        plot_graph(offx+x1, offy+y, width-60, height-y-30,
                   healthy, infected, cured, dead, settings, step);
}

static void draw_ui(struct settings *settings, int *state, bool step) {
        al_clear_to_color(settings->background_color);
        draw_ui_rectangle(0, 0, settings, state);
        draw_ui_panel(DISPLAYY, 0,
                      DISPLAYX-DISPLAYY, DISPLAYY,
                      settings, state, step);
}


/////////////////////////
/////[MAIN FUNCTION]/////
/////////////////////////

int main(int argc, char *argv[]) {
        // Initialize graphics
        must_init(al_init(), "allegro");

        // Initialize primitive drawing module
        must_init(al_init_primitives_addon(), "primitives");
        
        // Read settings from arguments
        struct settings settings;
        parse_args(argc, argv, &settings);
        
        // Seed RNG
        srand(settings.rng_seed);

        // Initialize keyboard
        must_init(al_install_keyboard(), "keyboard");

        // Create font
        settings.text_font = al_create_builtin_font();
        must_init(settings.text_font, "font");

        // Set up timers
        ALLEGRO_TIMER *draw_timer = al_create_timer(1.0 / 30.0);
        must_init(draw_timer, "draw timer");
        ALLEGRO_TIMER *simulation_timer = NULL;
        if (!settings.step_at_a_time) {
                simulation_timer = al_create_timer(settings.simulation_timestep);
                must_init(simulation_timer, "simulation timer");
        }

        // Set up event queue
        ALLEGRO_EVENT_QUEUE* queue = al_create_event_queue();
        must_init(queue, "queue");

        // Set up display
        al_set_new_display_option(ALLEGRO_SAMPLE_BUFFERS, 1, ALLEGRO_SUGGEST);
        al_set_new_display_option(ALLEGRO_SAMPLES, 8, ALLEGRO_SUGGEST);
        al_set_new_bitmap_flags(ALLEGRO_MIN_LINEAR | ALLEGRO_MAG_LINEAR);
        ALLEGRO_DISPLAY* display = al_create_display(DISPLAYX, DISPLAYY);
        must_init(display, "display");

        // Register events
        al_register_event_source(queue, al_get_keyboard_event_source());
        al_register_event_source(queue, al_get_display_event_source(display));
        al_register_event_source(queue, al_get_timer_event_source(draw_timer));
        if (simulation_timer != NULL) {
                al_register_event_source(queue, al_get_timer_event_source(simulation_timer));
        }

        // Start timers
        al_start_timer(draw_timer);
        if (simulation_timer != NULL) {
                al_start_timer(simulation_timer);
        }

        // Allocate and initialize memory for simulation
        size_t simulation_size = sizeof(int) *
                settings.simulation_grid_dimension *
                settings.simulation_grid_dimension;
        int *simulation_state = malloc(simulation_size);
        init_simulation(&settings, simulation_state);
        
        bool done = false;
        bool redraw = true;
        bool step = false;
        bool paused = false;
        ALLEGRO_EVENT event;
        for(;;) {
                al_wait_for_event(queue, &event);
                
                switch(event.type) {
                case ALLEGRO_EVENT_TIMER:
                        if (event.timer.source == draw_timer) {
                                redraw = true;
                        } else {
                                if (!paused) {
                                        simulation_state = simulation_step(&settings, simulation_state);
                                }
                        }
                        break;

                case ALLEGRO_EVENT_KEY_DOWN:
                        if (event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
                                done = true;
                        } else if (event.keyboard.keycode == ALLEGRO_KEY_SPACE) {
                                if (settings.step_at_a_time) {
                                        simulation_state = simulation_step(&settings, simulation_state);
                                        step = true;
                                } else {
                                        paused = !paused;
                                }
                        }
                        break;
                case ALLEGRO_EVENT_DISPLAY_CLOSE:
                        done = true;
                        break;
                }
                
                if(done) {
                        break;
                }
                
                if(redraw && al_is_event_queue_empty(queue)) {
                        draw_ui(&settings, simulation_state, !paused && (step || !settings.step_at_a_time));
                        step = false;
                        al_flip_display();
                        redraw = false;
                }
        }

        al_destroy_font(settings.text_font);
        al_destroy_display(display);
        al_destroy_timer(draw_timer);
        if (simulation_timer != NULL) {
                al_destroy_timer(simulation_timer);
        }
        al_destroy_event_queue(queue);
        
        return 0;
}
