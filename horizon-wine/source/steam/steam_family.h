#ifndef AUTORUN_STEAM_FAMILY_H
#define AUTORUN_STEAM_FAMILY_H
#include "steam_backend.h"
#include "steam_library.h"
/* Optional discovery. Failures preserve the owned library and have their own
 * warning. Does not borrow licenses, impersonate lenders or prove depot access. */
unsigned int steam_family_refresh(struct steam_backend *, struct steam_library *);
#endif
