/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * Clock profiles of a program: what the application's own section in Horizon
 * OC's settings becomes while that program runs. Horizon OC looks at the
 * running application's section before the owner's global profile, so a
 * profile chosen here has its way for this application only, and only while
 * it is the one running.
 *
 * A profile never asks for more than the global profile gives the same power
 * mode: the owner set those for the charger and the cooling the console has
 * in that mode. Where the global profile says nothing, neither does this.
 * So a profile lowers clocks, or keeps them and takes the processor's
 * governor out of the way. The governor follows the busiest core, and a
 * translated game is one busy thread the kernel moves between cores: no core
 * looks busy, and the game is given two thirds of the clock it was allowed
 * (Dishonored's title screen: one thread at 98 %, 1581 to 1963 MHz of 2499).
 *
 * Voltages are not touched: they are the kernel module's, set by its owner.
 *
 * Pure logic, so tests/clock_profile.c can drive it on the host.
 */
#ifndef WINE_NX_CLOCK_PROFILE_H
#define WINE_NX_CLOCK_PROFILE_H

#include <stdint.h>
#include <string.h>

/* As Horizon OC 2.5.1 numbers them (hocclk/board.h). */
enum { CLOCK_POWER_HANDHELD, CLOCK_POWER_CHARGING, CLOCK_POWER_CHARGING_USB, CLOCK_POWER_CHARGING_OFFICIAL,
       CLOCK_POWER_DOCKED, CLOCK_POWER_COUNT };
enum { CLOCK_CPU, CLOCK_GPU, CLOCK_MEMORY, CLOCK_GOVERNOR, CLOCK_DISPLAY, CLOCK_MODULE_COUNT };
#define CLOCK_GOVERNOR_CPU_OFF 1u   /* the processor's part of the governor word: disabled */
#define CLOCK_HOC_API 2
#define CLOCK_GLOBAL_TITLE 0xA111111111111111ull

/* MHz for each power mode and part; 0 leaves it to the global profile. */
struct clock_profile_list
{
    uint32_t mhz[CLOCK_POWER_COUNT][CLOCK_MODULE_COUNT];
};

struct clock_preset
{
    uint32_t cpu, gpu, memory;   /* MHz, values Horizon OC's tables have */
    int cpu_governor_off;        /* the processor stays at its clock whatever the load */
};

/* By the choice of the setting: 0 changes nothing. */
static const struct clock_preset clock_presets[] =
{
    { 0, 0, 0, 0 },
    { 1224, 460, 1600, 0 },    /* economy */
    { 1785, 768, 3000, 0 },    /* balanced */
    { 2091, 998, 3000, 1 },    /* performance */
    { 2397, 1228, 3000, 1 },   /* extreme: the most the owner's own profile asks for */
};
#define CLOCK_PRESET_COUNT (sizeof(clock_presets) / sizeof(clock_presets[0]))

/* What the global profile gives a power mode, found as Horizon OC finds it
 * (config.cpp, GetAutoClockHz): a charging mode that says nothing takes the
 * "charging" heading's value, then the handheld one. */
static inline uint32_t clock_profile_global( const struct clock_profile_list *global, int power, int part )
{
    if (global->mhz[power][part]) return global->mhz[power][part];
    if (power != CLOCK_POWER_CHARGING_USB && power != CLOCK_POWER_CHARGING_OFFICIAL) return 0;
    if (global->mhz[CLOCK_POWER_CHARGING][part]) return global->mhz[CLOCK_POWER_CHARGING][part];
    return global->mhz[CLOCK_POWER_HANDHELD][part];
}

static inline void clock_profile_build( const struct clock_preset *preset, const struct clock_profile_list *global,
                                        struct clock_profile_list *out )
{
    const uint32_t wanted[3] = { preset->cpu, preset->gpu, preset->memory };
    int power, part;

    memset( out, 0, sizeof(*out) );
    for (power = 0; power < CLOCK_POWER_COUNT; power++)
    {
        int limited = 0;

        if (power == CLOCK_POWER_CHARGING) continue;   /* a heading in Horizon OC's list, not a mode */
        for (part = CLOCK_CPU; part <= CLOCK_MEMORY; part++)
        {
            uint32_t ceiling = clock_profile_global( global, power, part );

            if (!ceiling) continue;
            out->mhz[power][part] = wanted[part] < ceiling ? wanted[part] : ceiling;
            if (part == CLOCK_CPU) limited = 1;
        }
        /* The application's governor word replaces the global one whole, so the
         * graphics and display parts of the owner's are carried over. With no
         * clock of the owner's to stay at, the governor is left as it is. */
        if (preset->cpu_governor_off && limited)
            out->mhz[power][CLOCK_GOVERNOR] = (clock_profile_global( global, power, CLOCK_GOVERNOR ) & ~0xffu) |
                                              CLOCK_GOVERNOR_CPU_OFF;
    }
}

#endif
