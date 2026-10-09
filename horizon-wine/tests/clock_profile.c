/* Clock profiles: never above the owner's global profile, governor carried over. */
#include "../source/clock_profile.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* The console's own global profile, 9 October 2026. */
    struct clock_profile_list global = { {
        [CLOCK_POWER_HANDHELD] = { 2397, 1228, 3000, 514, 0 },
        [CLOCK_POWER_CHARGING_USB] = { 1785, 921, 1600, 0, 0 },
        [CLOCK_POWER_CHARGING_OFFICIAL] = { 2499, 1228, 3000, 0, 0 },
        [CLOCK_POWER_DOCKED] = { 2397, 1075, 3000, 514, 0 },
    } }, out, none = { { { 0 } } };
    unsigned int i;
    int power, part;

    assert( CLOCK_PRESET_COUNT == 5 );
    /* Extreme: the owner's clocks, the processor's governor off, the rest of the word kept. */
    clock_profile_build( &clock_presets[4], &global, &out );
    assert( out.mhz[CLOCK_POWER_CHARGING_OFFICIAL][CLOCK_CPU] == 2397 );
    assert( out.mhz[CLOCK_POWER_CHARGING_OFFICIAL][CLOCK_GPU] == 1228 );
    assert( out.mhz[CLOCK_POWER_CHARGING_OFFICIAL][CLOCK_MEMORY] == 3000 );
    /* The charger's mode says nothing of the governor: the handheld word stands for it. */
    assert( out.mhz[CLOCK_POWER_CHARGING_OFFICIAL][CLOCK_GOVERNOR] == 513 );
    assert( out.mhz[CLOCK_POWER_CHARGING_USB][CLOCK_GOVERNOR] == 513 );
    assert( out.mhz[CLOCK_POWER_HANDHELD][CLOCK_GOVERNOR] == 513 );
    assert( out.mhz[CLOCK_POWER_DOCKED][CLOCK_GPU] == 1075 );
    assert( out.mhz[CLOCK_POWER_CHARGING_USB][CLOCK_CPU] == 1785 && out.mhz[CLOCK_POWER_CHARGING_USB][CLOCK_GPU] == 921 &&
            out.mhz[CLOCK_POWER_CHARGING_USB][CLOCK_MEMORY] == 1600 );
    /* Economy lowers, and leaves the governor alone. */
    clock_profile_build( &clock_presets[1], &global, &out );
    assert( out.mhz[CLOCK_POWER_HANDHELD][CLOCK_CPU] == 1224 && out.mhz[CLOCK_POWER_HANDHELD][CLOCK_GPU] == 460 &&
            out.mhz[CLOCK_POWER_HANDHELD][CLOCK_MEMORY] == 1600 && !out.mhz[CLOCK_POWER_HANDHELD][CLOCK_GOVERNOR] );
    /* No profile goes above the global one in any mode, or sets what it leaves unset. */
    for (i = 1; i < CLOCK_PRESET_COUNT; i++)
    {
        clock_profile_build( &clock_presets[i], &global, &out );
        for (power = 0; power < CLOCK_POWER_COUNT; power++)
        {
            for (part = CLOCK_CPU; part <= CLOCK_MEMORY; part++)
                assert( out.mhz[power][part] <= clock_profile_global( &global, power, part ) );
            assert( !out.mhz[power][CLOCK_DISPLAY] );
        }
        for (part = 0; part < CLOCK_MODULE_COUNT; part++) assert( !out.mhz[CLOCK_POWER_CHARGING][part] );
        /* A console with no global profile is left at its stock clocks. */
        clock_profile_build( &clock_presets[i], &none, &out );
        for (power = 0; power < CLOCK_POWER_COUNT; power++)
            for (part = 0; part < CLOCK_MODULE_COUNT; part++) assert( !out.mhz[power][part] );
    }
    puts( "Clock profiles: ceilings of the global profile, governor word and unset modes passed" );
    return 0;
}
