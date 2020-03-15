epidemics: epidemics.c
	$(CC) $< -Wall -Wextra -Wformat -Wshadow -Wpointer-arith -Wcast-qual -Wmissing-prototypes -Wimplicit-fallthrough -o $@ $(shell pkg-config allegro-5 allegro_font-5 allegro_primitives-5 --libs --cflags)
