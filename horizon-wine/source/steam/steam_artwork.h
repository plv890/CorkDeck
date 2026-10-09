#ifndef AUTORUN_STEAM_ARTWORK_H
#define AUTORUN_STEAM_ARTWORK_H
#include <SDL.h>
#include <stdint.h>
struct steam_artwork;
/* Public official Steam CDN only. Worker owns fetch/decode; UI owns textures.
 * Twelve bounded texture slots, one request, optional 64 MiB disk cache. */
struct steam_artwork *steam_artwork_create(const char *root);
void steam_artwork_request(struct steam_artwork *, uint32_t appid);
void steam_artwork_pause(struct steam_artwork *, int paused);
void steam_artwork_tick(struct steam_artwork *, SDL_Renderer *);
SDL_Texture *steam_artwork_texture(struct steam_artwork *, uint32_t, int *width,
                                   int *height);
void steam_artwork_destroy(struct steam_artwork *);
#endif
