/* Exercise the actual Steam registration boundary and unchanged first-add
 * renderer rule using the existing offline launcher fixtures. */
#define main launcher_fixture_main
#include "launcher_host.c"
#undef main
#include "../source/launcher.c"

int main(void)
{
    struct wine_nx_launcher_options options = {.runtime_dir="sdmc:/switch/wine",.machine_of=machine_of};
    struct launcher *l = calloc(1,sizeof(*l));
    struct program manual;
    struct launcher_kv kv;
    const char *exe="sdmc:/switch/wine/drive_c/Afterlight/Game.exe";
    const char *settings="sdmc:/switch/wine/drive_c/Afterlight/Game.wine-nx.txt";
    char value[128];
    assert(l);l->options=&options;carousel_fixture();launcher_catalog_init(&l->catalog);
    assert(!remove(settings));
    assert(describe_program(l,&manual,exe));
    apply_new_game_renderer(l,&manual);
    assert(manual.settings.d3d==LAUNCHER_D3D_WINE);
    assert(!remove(settings));
    assert(steam_register_executable(l,exe,"Steam display title"));
    assert(l->program_count==1&&l->programs[0].settings.d3d==manual.settings.d3d);
    assert(!strcmp(l->programs[0].title,"Steam display title"));
    assert(launcher_kv_load(&kv,settings));
    assert(launcher_kv_set(&kv,"title",NULL));
    assert(launcher_kv_set(&kv,"d3d","dxvk"));
    assert(launcher_kv_set(&kv,"d3d9","wine"));
    assert(launcher_kv_set(&kv,"future-setting","keep"));
    assert(launcher_kv_save(&kv,settings));load_program_settings(l,&l->programs[0]);
    assert(steam_register_executable(l,exe,"Steam updated title"));
    assert(launcher_kv_load(&kv,settings));
    assert(launcher_kv_get(&kv,"d3d",value,sizeof(value))&&!strcmp(value,"dxvk"));
    assert(launcher_kv_get(&kv,"d3d9",value,sizeof(value))&&!strcmp(value,"wine"));
    assert(launcher_kv_get(&kv,"future-setting",value,sizeof(value))&&!strcmp(value,"keep"));
    assert(steam_register_executable(l,exe,"Should not replace custom title"));
    assert(!strcmp(l->programs[0].title,"Steam updated title"));
    int catalog_count=l->program_count;
    assert(steam_launch_prerequisite(l,exe));
    assert(!strcmp(l->prerequisite_target,exe)&&l->program_count==catalog_count);
    assert(!steam_launch_prerequisite(l,exe));
    l->prerequisite_target[0]=0;
    assert(!steam_launch_prerequisite(l,"sdmc:/switch/wine/missing.exe"));
    assert(launcher_kv_load(&kv,settings)&&launcher_kv_get(&kv,"d3d",value,sizeof(value))&&!strcmp(value,"dxvk"));
    free(l);puts("Steam registration keeps manual first-add renderer behavior and explicit settings");
    return 0;
}
