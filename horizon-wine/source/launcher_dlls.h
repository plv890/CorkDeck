#ifndef LAUNCHER_DLLS_H
#define LAUNCHER_DLLS_H

#include <stddef.h>

/* The Windows DLLs on the card, from the DLL repository (horizon_dlls.h): a
 * check when the launcher starts, a screen to update and verify them, and the
 * question before a game starts on a card that has none. */
struct ui;
struct launcher_dlls;

/* Starts checking the repository in the background. */
struct launcher_dlls *launcher_dlls_create( struct ui *ui, const char *runtime_dir );
void launcher_dlls_tick( struct launcher_dlls *dlls );
void launcher_dlls_open( struct launcher_dlls *dlls );
/* 1 when the card can start a program. When it cannot, says so and offers the
 * Windows DLLs screen, and says whether the card can once back from it. Other
 * DLLs still to download do not stop a game. */
int  launcher_dlls_ready( struct launcher_dlls *dlls );
/* Whether the card can start a program, without asking anything. */
int  launcher_dlls_is_ready( struct launcher_dlls *dlls );
/* Whether its worker is on the card or the network right now. */
int launcher_dlls_busy( struct launcher_dlls *d );
/* Download what is new or changed, after asking; for quick setup. */
int  launcher_dlls_install( struct launcher_dlls *dlls );
/* A sentence on what the card has and what downloading would take. */
const char *launcher_dlls_describe( struct launcher_dlls *dlls, char *buffer, size_t size );
/* What Settings shows beside the row: tone is enum ui_value_tone. */
const char *launcher_dlls_status( struct launcher_dlls *dlls, char *buffer, size_t size, int *tone );
void launcher_dlls_destroy( struct launcher_dlls *dlls );

#endif
