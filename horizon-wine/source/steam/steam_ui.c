#include "steam_ui.h"
#include "steam_provider.h"
#include "steam_wire.h"
#include "steam_library_ui.h"
#include "steam_settings.h"
#include "steam_diagnostics.h"
#include "../diagnostics/lan_log.h"
#include "../../vendor/qrcodegen/qrcodegen.h"
#include "../launcher_ui.h"
#include <stdio.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
static struct steam_provider *provider;
static struct steam_launcher_adapter launcher;
static uint32_t registered_app,removed_generation;
static Uint32 diagnostic_tick;
static uint32_t diagnostic_app;
static enum steam_download_state diagnostic_state;
static unsigned int diagnostic_error;
static int diagnostic(const char *,const struct steam_status_view *);
static void removal_diagnostic(const char *root,const struct steam_remove_view *v){
    char path[768];FILE *f;int n=snprintf(path,sizeof(path),"%s/logs/steam-storage.log",root);
    if(n<0||(size_t)n>=sizeof(path)||(f=fopen(path,"w"))==NULL)return;
    fprintf(f,"steam_remove_v1 appid=%u error=%u busy=%u generation=%u\n",v->app_id,v->error,(unsigned int)v->busy,v->generation);fclose(f);
}
void steam_ui_prepare(const char *root)
{if(!provider)provider=steam_provider_create();if(provider)steam_provider_set_root(provider,root);}
void steam_ui_set_launcher(const struct steam_launcher_adapter *a){launcher=*a;}
void steam_ui_shutdown(void)
{steam_provider_destroy(provider);provider=NULL;registered_app=0;removed_generation=0;diagnostic_tick=0;diagnostic_app=0;diagnostic_state=STEAM_DOWNLOAD_IDLE;diagnostic_error=0;}
static int prompt(const char *title,int secret,char *out,size_t capacity)
{
    memset(out,0,capacity);
#ifdef __SWITCH__
    SwkbdConfig keyboard;Result rc;
    if(R_FAILED(swkbdCreate(&keyboard,0)))return 0;
    if(secret)swkbdConfigMakePresetPassword(&keyboard);else swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard,title);swkbdConfigSetSubText(&keyboard,title);swkbdConfigSetGuideText(&keyboard,title);swkbdConfigSetStringLenMax(&keyboard,(u32)(capacity-1));
    rc=swkbdShow(&keyboard,out,capacity);swkbdClose(&keyboard);
    steam_secret_clear(&keyboard,sizeof(keyboard));
    if(R_FAILED(rc)){steam_secret_clear(out,capacity);return 0;}return out[0]!=0;
#else
    (void)title;(void)secret;return 0;
#endif
}
static const char *status_text(enum steam_status state)
{
    switch(state){
    case STEAM_CONNECTING:return "Connecting securely to Steam...";
    case STEAM_AUTHENTICATING:return "Authenticating with Steam...";
    case STEAM_NEED_GUARD:return "Steam Guard: press A to enter a code, or approve in the Steam app.";
    case STEAM_CONFIRM_DEVICE:return "Waiting for Steam confirmation...";
    case STEAM_LOGGING_ON:return "Confirming account session with Steam...";
    case STEAM_ONLINE:return "OK - Signed in to Steam";
    case STEAM_ERROR:return "Steam sign-in failed. See the error below.";
    default:return "Not signed in";
    }
}
static const char *error_text(unsigned int code)
{
    switch(code){
    case 1:return "Steam connection cancelled.";
    case 2:return "Steam connection closed or network I/O failed.";
    case 102:return "No internet connection. Check the console's Wi-Fi and try again.";
    case 103:return "Secure connection to Steam failed. Check the console's date and time.";
    case 104:case 106:return "Steam is not answering correctly right now. Try again later.";
    case 11:return "Invalid Steam logon response.";
    case 12:return "Steam logon identity did not match the authenticated account.";
    case 13:return "Steam did not provide a valid client session.";
    case 1005:return "Steam rejected the password. Check account name and password.";
    case 1065:case 1088:return "Incorrect Steam Guard code. Press A to try again.";
    case 1027:return "Authentication expired. Sign in again.";
    case 1084:return "Steam rate limit reached. Wait before trying again.";
    case 1015:return "Steam denied this request.";
    case 10:return "Steam requires an unsupported confirmation method.";
    case 9:return "Unable to encrypt password safely.";
    case 6:return "Steam did not respond before the timeout.";
    case 3:return "Unexpected Steam protocol response.";
    case 8:return "WebSocket handshake rejected.";
    case 4:return "Steam ended or rejected the connection.";
    case 5:return "Unable to allocate Steam connection resources.";
    case 3401:return "Cannot read or save the local session vault. Check SD.";
    case 3402:return "Saved session vault is invalid. Sign in again.";
    case 3403:return "Wrong local vault passphrase or damaged vault. Retry unlock.";
    case 3404:return "Secure local session encryption failed.";
    case 3405:return "Use a local vault passphrase of 12 to 127 characters.";
    default:return "Steam operation failed. See steam-diagnostic.log for the failed stage.";
    }
}
static void session_action(struct ui *ui,int save)
{
    char pass[128]={0},confirm[128]={0};
    int automatic=0;
    if(save){
        static const char *const choices[]={"Automatic on this console","Local passphrase"};
        int choice=ui_menu(ui,"Save Steam session",choices,2,0);
        if(choice<0)return;
        automatic=choice==0;
        if(automatic&&!ui_confirm(ui,"Automatic Steam sign-in",
            "Encrypt the refresh token with this console's device key. "
            "CorkDeck reconnects after games, restart and updates. Copying only "
            "the SD card cannot unlock it. Other privileged homebrew on this "
            "console can access it. Logout deletes the saved session.","Enable"))return;
    }else{
        struct steam_session_view v;steam_provider_session_status(provider,&v);
        if(v.automatic){
            if(!steam_provider_resume_session(provider))ui_message(ui,"Steam session","Could not reconnect saved session.");
            return;
        }
    }
    if(automatic){
        if(!steam_provider_save_automatic_session(provider)){ui_message(ui,"Steam session","Sign in before saving.");return;}
        goto saving;
    }
    if(save && !ui_confirm(ui,"Save Steam session",
        "Choose a separate local passphrase (12+ characters), not your Steam password. "
        "It encrypts the refresh token on SD. Unlock it once after restarting CorkDeck. "
        "This mode requires manual unlock.","Continue"))return;
    if(!prompt(save?"LOCAL VAULT - new passphrase (12+ characters)":"LOCAL VAULT - unlock saved Steam session",1,pass,sizeof(pass)))goto done;
    if(save){
        if(!prompt("LOCAL VAULT - repeat new passphrase",1,confirm,sizeof(confirm)))goto done;
        if(strcmp(pass,confirm)){ui_message(ui,"Local vault","Passphrases do not match.");goto done;}
    }
    if(!(save?steam_provider_save_session(provider,pass):steam_provider_unlock_session(provider,pass)))
        ui_message(ui,"Steam session","Could not start. Use 12-127 characters and check the account status.");
    else if(!save)ui_toast(ui,"Unlocking saved session and connecting...",4000);
    else {
saving:;
        struct steam_session_view v;
        ui_start_screen(ui);
        while(ui_begin_frame(ui)) {
            struct ui_input input;
            while(ui_poll(ui,&input))
                if(input.button==UI_B)ui_toast(ui,"Wait for the session save result.",2000);
            steam_provider_session_status(provider,&v);
            if(!v.saving){
                ui_message(ui,"Steam session",v.error?error_text(v.error):v.present?
                    (v.automatic?"Saved for automatic sign-in on this console.":"Saved on SD. Unlock with your local passphrase after restarting CorkDeck."):
                    "Session was not saved. Check connection and retry.");
                break;
            }
            ui_background(ui);ui_header_back(ui,"Saving Steam session",NULL);
            ui_text_wrapped(ui,ui->normal,ui->width/2,290,960,3,
                "Encrypting the refresh token and saving it on SD...",ui->text,1);
            ui_present(ui);ui_wait(ui);
        }
        ui_start_screen(ui);
    }
done:
    steam_secret_clear(pass,sizeof(pass));steam_secret_clear(confirm,sizeof(confirm));
}
static char launcher_root[256];
static void publish_transfer(const struct steam_download_view *d)
{
    char safe[2048];
    snprintf(safe, sizeof(safe),
             "Steam build %s\nappid=%u state=%u error=%u diagnostic_error=%u cleanup_errno=%u\n"
             "verified=%llu total=%llu network=%llu free=%llu free_known=%u rate_bps=%.0f\n"
             "elapsed_ms=%llu network_ms=%llu decode_ms=%llu write_ms=%llu sync_ms=%llu\n"
             "first_byte_ms=%llu body_ms=%llu verify_ms=%llu resume_ms=%llu writer_wait_ms=%llu\n"
             "requests=%u connections=%u parallelism=%u peak_requests=%u batch_files=%u batch_peak_files=%u integrity_detail=%u chunk_format=%u integrity_retries=%u depot_id=%u\n",
             STEAM_DIAGNOSTIC_BUILD_LABEL, d->app_id, (unsigned)d->state, d->error,
             d->diagnostic_error, d->cleanup_errno, (unsigned long long)d->downloaded,
             (unsigned long long)d->total, (unsigned long long)d->network_bytes,
             (unsigned long long)d->free_bytes, (unsigned)!!d->free_known,
             d->bytes_per_second, (unsigned long long)d->elapsed_ms,
             (unsigned long long)d->network_ms, (unsigned long long)d->decode_ms,
             (unsigned long long)d->write_ms, (unsigned long long)d->sync_ms,
             (unsigned long long)d->first_byte_ms, (unsigned long long)d->body_ms,
             (unsigned long long)d->verify_ms, (unsigned long long)d->resume_ms,
             (unsigned long long)d->writer_wait_ms,
             d->requests, d->connections, d->parallelism, d->peak_requests, d->batch_files, d->batch_peak_files,d->integrity_detail,d->chunk_format,d->integrity_retries,d->depot_id);
    lan_log_publish(safe);
}
void steam_ui_tick(struct ui *ui,const char *root)
{
    struct steam_download_view d;struct steam_library_view l;
    struct steam_status_view auth;
    struct steam_game rows[8];size_t offset,count,total,i;
    if(!provider)return;
    steam_provider_download_status(provider,&d);steam_provider_library_status(provider,&l);
    publish_transfer(&d);
    if(root&&!launcher_root[0])snprintf(launcher_root,sizeof(launcher_root),"%s",root);
    switch(steam_provider_restart_poll(provider)){
    case 1:ui_toast(ui,"The unfinished files are removed; the download starts over",4000);break;
    case -1:ui_toast(ui,"The download could not be started over. Install the game from its page.",5000);break;
    default:break;}
    {struct steam_remove_view removal;steam_provider_remove_status(provider,&removal);
     if(!removal.app_id)removed_generation=0;
     else if(!removal.busy&&removal.app_id&&removed_generation!=removal.generation){removed_generation=removal.generation;registered_app=0;removal_diagnostic(root,&removal);
      if(removal.error)ui_toast(ui,"Steam uninstall failed. Files in Steam trash are preserved for retry.",6000);
      else if(launcher.unregister_directory&&!launcher.unregister_directory(launcher.context,removal.directory))ui_toast(ui,"Files removed; catalog save failed. Remove the missing entry in CorkDeck.",6000);
      else ui_toast(ui,"Steam game uninstalled.",4000);}}

    if (!diagnostic_tick || SDL_GetTicks()-diagnostic_tick >= 5000 ||
        diagnostic_app != d.app_id || diagnostic_error != d.error ||
        (diagnostic_state != d.state && (d.state == STEAM_DOWNLOAD_COMPLETE ||
         d.state == STEAM_DOWNLOAD_CANCELLED || d.state == STEAM_DOWNLOAD_ERROR)))
    {
        steam_provider_status(provider,&auth);diagnostic(root,&auth);
        diagnostic_tick=SDL_GetTicks();diagnostic_app=d.app_id;
        diagnostic_state=d.state;diagnostic_error=d.error;
    }
    if(d.state!=STEAM_DOWNLOAD_COMPLETE){registered_app=0;return;}
    if(registered_app==d.app_id||!d.executable[0]||!launcher.register_executable)return;
    registered_app=d.app_id;
    for(offset=0;offset<l.count;offset+=8){
        count=steam_provider_library_page(provider,"",offset,rows,8,&total);
        for(i=0;i<count;++i)if(rows[i].app_id==d.app_id){
            if(!launcher.register_executable(launcher.context,d.executable,rows[i].name))
                ui_toast(ui,"Steam installation complete. Add executable from Installed.",5000);
            else ui_toast(ui,"Steam game installed and added to CorkDeck.",4000);
            return;
        }
    }
}
int steam_ui_library(struct ui *ui,const char *root,int downloads)
{
    struct steam_status_view v;struct steam_session_view vault;
    steam_ui_prepare(root);if(!provider){ui_message(ui,"Steam","Unable to create provider.");return -1;}
    steam_provider_status(provider,&v);steam_provider_session_status(provider,&vault);
    if(v.state==STEAM_OFFLINE||v.state==STEAM_ERROR){
        if(vault.present)session_action(ui,0);
        else {ui_message(ui,"Steam library","Link your Steam account in Settings > Steam settings > Sign in to Steam.");return -1;}
    }
    return steam_library_ui_page(ui,provider,root,&launcher,downloads?2:0);
}
int steam_ui_install_language_picker(struct ui *ui,uint64_t available,char out[32]) {
    struct ui_row rows[32]={0};struct ui_list list={0};
    const char *ids[32];size_t i;int count=0;
    out[0]=0;
    for(i=0;i<steam_language_count && i<63;++i)if(available & (UINT64_C(1)<<i)){
        ids[count]=steam_languages[i].id;
        snprintf(rows[count].label,sizeof(rows[count].label),"%s",steam_languages[i].name);
        rows[count].help="Use this language for this installation only. Steam settings stay unchanged.";
        ++count;
    }
    /* Only for a game with no language files to choose from: with them, the
     * files common to every language are not a game that starts. */
    if(!count && (available & STEAM_LANGUAGE_BASE)){
        ids[count]=STEAM_LANGUAGE_BASE_ID;
        snprintf(rows[count].label,sizeof(rows[count].label),"Default / base files");
        rows[count].help="Only language-neutral depots. The game's built-in language is unknown; optional translations are excluded.";
        ++count;
    }
    if(!count){ui_message(ui,"Download language","Steam lists no recognized language choice for these Windows depots.");return 0;}
    if(ui_list_run(ui,&list,"Choose language for this game","Preferred language unavailable; choose an alternative",rows,count,0)!=UI_ACTION_CHOOSE)return 0;
    snprintf(out,32,"%s",ids[list.selection]);return 1;
}
int steam_ui_language_picker(struct ui *ui,const char *current){
    struct ui_row languages[32]={0};struct ui_list list={0};size_t i;
    snprintf(languages[0].label,sizeof(languages[0].label),"System language");
    snprintf(languages[0].value,sizeof(languages[0].value),"Follow console");
    for(i=0;i<steam_language_count&&i+1<32;++i){snprintf(languages[i+1].label,sizeof(languages[i+1].label),"%s",steam_languages[i].name);
        snprintf(languages[i+1].value,sizeof(languages[i+1].value),"%s",steam_languages[i].id);if(!strcmp(current,steam_languages[i].id))list.selection=(int)i+1;}
    return ui_list_run(ui,&list,"Download language","Scroll with Up/Down or touch; A selects",languages,(int)i+1,0)==UI_ACTION_CHOOSE?list.selection:-1;
}
void steam_ui_settings(struct ui *ui, const char *root) {
  struct ui_list list = {0};
  steam_ui_prepare(root);
  if (!provider) {
    ui_message(ui, "Steam", "Unable to create provider.");
    return;
  }
  for (;;) {
    struct ui_row rows[9] = {0};
    struct steam_status_view auth;
    struct steam_session_view vault;
    char language[32];
    unsigned int concurrency;
    enum ui_action action;
    steam_provider_status(provider, &auth);
    steam_provider_session_status(provider, &vault);
    steam_provider_language_preference(provider, language);
    concurrency = steam_provider_concurrency(provider);
    snprintf(rows[0].label, sizeof(rows[0].label), "Sign in to Steam");
    snprintf(rows[0].value, sizeof(rows[0].value), "%s",
             auth.state == STEAM_ONLINE ? "OK - signed in"
                                        : status_text(auth.state));
    rows[0].value_tone =
        auth.state == STEAM_ONLINE ? UI_VALUE_SUCCESS : UI_VALUE_NORMAL;
    rows[0].help = "QR or account/password sign-in. Signing in alone does not "
                   "save a session.";
    snprintf(rows[1].label, sizeof(rows[1].label), "Save encrypted session");
    snprintf(rows[1].value, sizeof(rows[1].value), "%s",
             vault.saving                 ? "Saving..."
             : vault.error                ? "Save failed"
             : vault.present              ? (vault.automatic?"Automatic on this console":"Saved on SD")
             : auth.state == STEAM_ONLINE ? "Not saved - this run only"
                                          : "Sign in first");
    rows[1].disabled = auth.state != STEAM_ONLINE || vault.saving;
    rows[1].value_tone = vault.error     ? UI_VALUE_DANGER
                         : vault.present ? UI_VALUE_SUCCESS
                                         : UI_VALUE_NORMAL;
    rows[1].help = "Encrypt the refresh token with a separate local "
                   "passphrase. No Steam password is saved.";
    snprintf(rows[2].label, sizeof(rows[2].label), "Unlock saved session");
    snprintf(rows[2].value, sizeof(rows[2].value), "%s",
             auth.state == STEAM_ONLINE ? "Already signed in"
             : vault.present            ? (vault.automatic?"Reconnect saved session":"Enter local passphrase")
                                        : "No saved session");
    rows[2].disabled = !vault.present || auth.state == STEAM_ONLINE;
    rows[2].help = "Automatic sessions reconnect at startup. Passphrase sessions require local unlock.";
    snprintf(rows[3].label, sizeof(rows[3].label), "Download language");
    snprintf(rows[3].value, sizeof(rows[3].value), "%s", language);
    rows[3].help =
        "Choose the language of official Windows depots for new installations.";
    snprintf(rows[4].label, sizeof(rows[4].label), "Parallel CDN requests");
    snprintf(rows[4].value, sizeof(rows[4].value), "%u", concurrency);
    rows[4].help = "1 to 16 connections over several Steam servers. Change "
                   "between downloads; default is 16.";
    snprintf(rows[5].label, sizeof(rows[5].label),
             "Log out / Delete local session");
    rows[5].destructive = 1;
    rows[5].help = "Remove local encrypted credentials. Installed games remain "
                   "available to CorkDeck.";
    snprintf(rows[6].label, sizeof(rows[6].label), "Export diagnostics");
    snprintf(rows[6].value, sizeof(rows[6].value), "%s", STEAM_DIAGNOSTIC_BUILD_LABEL);
    rows[6].help = "Write and read back logs/steam-report.log. Export after a transfer, before Play. No credentials are exported.";
    char lan_url[128];
    int lan_enabled = lan_log_url(lan_url, sizeof(lan_url));
    snprintf(rows[7].label, sizeof(rows[7].label), "LAN diagnostics (temporary)");
    snprintf(rows[7].value, sizeof(rows[7].value), "%s", lan_enabled ? "Enabled" : "Disabled");
    rows[7].help = "Read-only browser view of transfers and runtime/game logs. Trusted LAN only; plain HTTP. Stops when CorkDeck exits.";
    snprintf(rows[8].label, sizeof(rows[8].label), "About Steam integration");
    action = ui_list_run(ui, &list, "Steam settings",
                         "Account and session / Downloads", rows, 9, 0);
    if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT)
      break;
    if (action != UI_ACTION_CHOOSE)
      continue;
    switch (list.selection) {
    case 0:
      steam_ui_open(ui, root);
      break;
    case 1:
      session_action(ui, 1);
      break;
    case 2:
      session_action(ui, 0);
      break;
    case 3: {
      int choice = steam_ui_language_picker(ui, language);
      if (choice >= 0 &&
          !steam_provider_set_language(provider, choice?steam_languages[choice-1].id:"system"))
        ui_message(ui, "Steam settings",
                   "Could not save language. Finish the active download and "
                   "check SD.");
      break;
    }
    case 4: {
      static const unsigned int counts[] = {1, 2, 4, 8, 12, 16};
      const char *choices[] = {"1 - Serial",      "2",  "4", "8",
                               "12",               "16 - Recommended"};
      int n, selected = 5;
      for (n = 0; n < 6; ++n)
        if (counts[n] == concurrency)
          selected = n;
      n = ui_menu(ui, "Parallel CDN requests", choices, 6, selected);
      if (n >= 0 && !steam_provider_set_concurrency(provider, counts[n]))
        ui_message(ui, "Steam settings",
                   "Finish the download before changing concurrency; check SD "
                   "if saving failed.");
      break;
    }
    case 5:
      if (ui_confirm(ui, "Steam logout",
                     "Delete the local session? Installed games are preserved.",
                     "Log out")) {
        steam_provider_logout(provider);
        steam_provider_session_status(provider, &vault);
        if (vault.error)
          ui_message(ui, "Steam logout",
                     "Session stopped, but local deletion failed. Check SD and "
                     "retry.");
      }
      break;
    case 6: {
      struct steam_diagnostic_view snapshot = {0};
      struct steam_diagnostic_result result;
      char text[512];
      static const char *history[] = {"missing", "legacy v1", "current v2", "unknown", "unreadable"};
      steam_provider_status(provider, &snapshot.auth);
      steam_provider_library_status(provider, &snapshot.library);
      steam_provider_session_status(provider, &snapshot.session);
      steam_provider_download_status(provider, &snapshot.download);
      snapshot.concurrency = steam_provider_concurrency(provider);
      if (steam_diagnostic_export(root, &snapshot, &result)) {
        snprintf(text, sizeof(text), "Steam build %s. Export failed: filesystem error %u, stage %u. Check SD and retry.",
                 STEAM_DIAGNOSTIC_BUILD_LABEL, result.error, result.stage);
      } else {
        snprintf(text, sizeof(text),
                 "Steam build %s. Report written, synchronized and read back: logs/steam-report.log. Transfer history: %s (read error %u). %s",
                 STEAM_DIAGNOSTIC_BUILD_LABEL, history[result.history], result.history_error,
                 snapshot.download.app_id && snapshot.download.state != STEAM_DOWNLOAD_IDLE
                     ? "Current transfer snapshot included. Copy this report from the active Wine/logs folder."
                     : "No transfer in memory. Export after a download/cancellation, before Play or restart.");
      }
      ui_message(ui, "Steam diagnostics", text);
      break;
    }
    case 7: {
      const char *choices[] = {lan_enabled ? "Show browser URL" : "Enable for this run", "Disable"};
      int choice = ui_menu(ui, "LAN diagnostics", choices, 2, 0);
      if (choice == 1) lan_log_stop();
      if (choice == 0) {
        unsigned int error = lan_log_start(root, 8181);
        char text[512];
        if (error || !lan_log_url(lan_url, sizeof(lan_url)))
          snprintf(text, sizeof(text), "Could not start LAN diagnostics: error %u. Check Wi-Fi and retry.", error);
        else snprintf(text, sizeof(text), "Open on a computer on the same trusted LAN: %s  Include the full address after #. Plain HTTP; local paths and game names may appear. Disabled after CorkDeck exits/restarts. During a game, refresh the log list to select its log.", lan_url);
        ui_message(ui, "LAN diagnostics", text);
      }
      break;
    }
    case 8:
      ui_message(
          ui, "Steam",
          "Steam provider build " STEAM_DIAGNOSTIC_BUILD_LABEL ". Steam library and Downloads are in the main header. A green account "
          "OK means signed in now. Saved on SD means encrypted persistence; "
          "device-bound sessions reconnect automatically; passphrase sessions require unlock. Downloaded games use the unchanged "
          "CorkDeck launcher. Compatibility remains Unknown until tested.");
      break;
    }
  }
  ui_start_screen(ui);
}
static int diagnostic(const char *root,const struct steam_status_view *v)
{
    char path[768];int n,written,closed;FILE *f;
    n=snprintf(path,sizeof(path),"%s/logs/steam-diagnostic.log",root);
    if(n<0||(size_t)n>=sizeof(path)||(f=fopen(path,"w"))==NULL)return 0;
    written=fprintf(f,"steam_auth_v2 state=%u error=%u guard_type=%u failed_stage=%u cm_type=%u transport_error=%u\n",(unsigned int)v->state,v->error,v->guard_type,v->failed_stage,v->cm_type,v->transport_error);
    closed=fclose(f);return written>0&&!closed;
}
/* A field of the sign-in form: its name over a panel, lit round when in focus. */
static void form_field(struct ui *ui,int y,const char *label,const char *value,int selected,int filled)
{
    SDL_Color blue={26,159,255,255},field={33,44,58,255};
    ui_text(ui,ui->small,108,y,label,ui->dim);
    ui_rounded(ui,104,y+30,530,56,6,field);
    if(selected)ui_outline(ui,101,y+27,536,62,8,3,blue);
    ui_text_fit(ui,ui->normal,122,y+30+(56-TTF_FontHeight(ui->normal))/2,494,value,filled?ui->value:ui->dim,0);
}
void steam_ui_open(struct ui *ui,const char *root)
{
    struct steam_status_view view={0},previous={.state=(enum steam_status)-1};
    char account[128]={0},password[256]={0},challenge[512]={0},last[512]={0};
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)]={0},temp[qrcodegen_BUFFER_LEN_FOR_VERSION(12)]={0};
    int saved=1,selected=3,valid_qr=0;
    SDL_Color blue={26,159,255,255},panel={24,33,44,255},white={255,255,255,255},black={0,0,0,255};
    if(!provider)provider=steam_provider_create();
    if(!provider){ui_message(ui,"Steam","Unable to create account provider.");return;}
    if(!steam_provider_set_root(provider,root)){ui_message(ui,"Steam","Invalid installation root.");return;}
    steam_provider_status(provider,&view);
    {struct steam_session_view vault;steam_provider_session_status(provider,&vault);
    if(view.state==STEAM_OFFLINE&&!vault.present)steam_provider_login_qr(provider);}
    ui_start_screen(ui);
    while(ui_begin_frame(ui)) {
        struct ui_input input;char info[160];
        struct steam_session_view vault;steam_provider_session_status(provider,&vault);
        steam_provider_status(provider,&view);
        if(view.state!=previous.state||view.error!=previous.error){saved=diagnostic(root,&view);previous=view;}
        while(ui_poll(ui,&input)) {
            if(input.touch==UI_TOUCH_TAP&&input.y<80)input.button=UI_B;
            if(input.button==UI_B){
                if(vault.saving){ui_toast(ui,"Saving session. Wait for Saved on SD before leaving.",3000);break;}
                if(view.state==STEAM_ONLINE&&!vault.present){
                    if(ui_confirm(ui,"Stay signed in?","Keep the sign-in on this console, encrypted with its own key, so CorkDeck reconnects by itself. Signing out deletes it.", "Stay signed in")){
                        if(steam_provider_save_automatic_session(provider))ui_toast(ui,"Saving the sign-in...",3000);
                        else ui_toast(ui,"Could not save the sign-in",3000);
                    }
                }
                goto done;
            }
            if(input.button==UI_X){steam_provider_logout(provider);steam_secret_clear(password,sizeof(password));break;}
            if(input.button==UI_PLUS){session_action(ui,view.state==STEAM_ONLINE);break;}
            if(input.button==UI_UP)selected=(selected+3)%4;
            if(input.button==UI_DOWN)selected=(selected+1)%4;
            if(input.button==UI_LEFT)selected=0;
            if(input.button==UI_RIGHT)selected=3;
            if(input.touch==UI_TOUCH_TAP){
                if(input.x>=104&&input.x<634){
                    if(input.y>=270&&input.y<338)selected=0;
                    else if(input.y>=365&&input.y<433)selected=1;
                    else if(input.y>=470&&input.y<526)selected=2;
                    else continue;
                }else if(input.x>=790&&input.x<1140&&input.y>=230&&input.y<540)selected=3;
                else continue;
                input.button=UI_A;
            }
            if(input.button!=UI_A)continue;
            if(view.state==STEAM_NEED_GUARD){
                char code[17];
                if(prompt(view.guard_type==2?"Steam Guard - email code":"Steam Guard - authenticator code",1,code,sizeof(code))){
                    size_t i;for(i=0;code[i];++i)if(code[i]>='a'&&code[i]<='z')code[i]-='a'-'A';
                    if(!steam_provider_guard(provider,code))ui_message(ui,"Steam Guard","Enter the code using letters and digits.");
                }
                steam_secret_clear(code,sizeof(code));break;
            }
            if(view.state==STEAM_ONLINE)continue;
            if(selected==0){prompt("Steam - ACCOUNT NAME (login)",0,account,sizeof(account));break;}
            if(selected==1){prompt("Steam - PASSWORD (hidden)",1,password,sizeof(password));break;}
            if(selected==2){
                if(!account[0]||!password[0]){ui_message(ui,"Steam sign in","Fill in ACCOUNT NAME and PASSWORD first.");selected=account[0]?1:0;break;}
                if(!steam_provider_login(provider,account,password))ui_message(ui,"Steam","Unable to start sign-in.");
                steam_secret_clear(password,sizeof(password));break;
            }
            if(selected==3){if(vault.present)session_action(ui,0);else steam_provider_login_qr(provider);break;}
        }
        steam_provider_status(provider,&view);steam_provider_challenge(provider,challenge);
        if(view.state==STEAM_ONLINE)steam_secret_clear(password,sizeof(password));
        if(strcmp(challenge,last)){
            steam_secret_clear(qr,sizeof(qr));steam_secret_clear(temp,sizeof(temp));valid_qr=0;
            if(challenge[0])valid_qr=qrcodegen_encodeText(challenge,temp,qr,qrcodegen_Ecc_MEDIUM,1,12,qrcodegen_Mask_AUTO,true);
            strcpy(last,challenge);steam_secret_clear(temp,sizeof(temp));
        }
        ui_background(ui);ui_header_back(ui,"Steam account",NULL);
        if(view.state!=STEAM_ONLINE){
            int title_h=TTF_FontHeight(ui->normal);
            /* Left: the account's name and password. Right: the code for the phone. */
            ui_rounded(ui,80,120,590,440,8,panel);
            ui_text(ui,ui->normal,108,144,vault.present?"Unlock saved Steam session (+)":"Sign in to Steam",ui->value);
            ui_text(ui,ui->small,108,144+title_h+8,"With the account's name and password",ui->dim);
            form_field(ui,240,"ACCOUNT NAME",account[0]?account:"Select to enter your Steam login",selected==0,account[0]!=0);
            form_field(ui,335,"PASSWORD",password[0]?"********":"Select to enter your password",selected==1,password[0]!=0);
            ui_rounded(ui,104,470,530,56,6,blue);
            if(selected==2)ui_outline(ui,101,467,536,62,8,3,white);
            ui_text_centered(ui,ui->normal,104+265,470+(56-title_h)/2,"Sign in",(SDL_Color){4,18,31,255});

            ui_rounded(ui,700,120,500,440,8,panel);
            ui_text(ui,ui->normal,728,144,vault.present?"SAVED STEAM SESSION":"OR SIGN IN WITH QR",ui->value);
            if(valid_qr){
                int size=qrcodegen_getSize(qr),scale=240/(size+8),span=(size+8)*scale;
                int ox=830+(240-span)/2,oy=206+(240-span)/2,x,y;
                ui_rounded(ui,ox-10,oy-10,span+20,span+20,8,white);
                if(selected==3)ui_outline(ui,ox-14,oy-14,span+28,span+28,10,3,blue);
                for(y=0;y<size;++y)for(x=0;x<size;++x)if(qrcodegen_getModule(qr,x,y))ui_fill(ui,ox+(x+4)*scale,oy+(y+4)*scale,scale,scale,black);
            }else{
                ui_rounded(ui,830,206,240,240,8,(SDL_Color){33,44,58,255});
                if(selected==3)ui_outline(ui,827,203,246,246,10,3,blue);
                ui_text_wrapped(ui,ui->normal,950,290,208,3,vault.present?"Unlock saved session":challenge[0]?"QR cannot be displayed. Use password.":"Generate QR code",ui->value,1);
            }
            ui_text_wrapped(ui,ui->small,950,476,440,3,vault.present?"Unlock with your LOCAL vault passphrase. X deletes the saved session.":"Scan with the Steam mobile app and confirm sign in.",ui->dim,1);
        }else{
            ui_rounded(ui,80,120,1120,300,8,panel);
            ui_rounded(ui,108,152,12,12,6,ui->success);
            ui_text(ui,ui->large,134,140,"OK - Signed in to Steam",ui->value);
            snprintf(info,sizeof(info),"SteamID: %llu",(unsigned long long)view.steam_id);
            ui_text(ui,ui->normal,108,140+TTF_FontHeight(ui->large)+24,info,ui->text);
            ui_text_wrapped(ui,ui->small,108,140+TTF_FontHeight(ui->large)+24+TTF_FontHeight(ui->normal)+20,1060,3,
                            "The account's games are in the Library, on the Not installed tab. Their downloads are in Downloads.",ui->dim,0);
            ui_text(ui,ui->small,108,372,"Press X to log out of this session.",ui->dim);
        }
        if(view.state!=STEAM_ONLINE)ui_text(ui,ui->small,104,578,status_text(view.state),ui->text);
        if(view.error){snprintf(info,sizeof(info),"Error %u (stage %u, CM %u): %s",view.error,view.failed_stage,view.cm_type,ui->translate?ui->translate(error_text(view.error)):error_text(view.error));ui_text_wrapped(ui,ui->small,104,604,1060,2,info,ui->danger,0);}
        else if(vault.error){snprintf(info,sizeof(info),"Session storage error %u: %s",vault.error,ui->translate?ui->translate(error_text(vault.error)):error_text(vault.error));ui_text_fit(ui,ui->small,104,604,1060,info,ui->danger,0);}
        else ui_text_fit(ui,ui->small,104,604,1060,vault.saving?"Encrypting local session...":vault.present?"Encrypted local session saved. + saves/reconnects/unlocks; X deletes it.":"Session is in memory. + saves an encrypted session (automatic or passphrase).",ui->dim,0);
        if(!saved)ui_text(ui,ui->small,104,630,"Could not write logs/steam-diagnostic.log",ui->danger);
        {
            struct ui_hint hints[]={{UI_A,view.state==STEAM_NEED_GUARD?"Enter Guard code":"Select"},{UI_PLUS,"Save / Unlock"},{UI_X,"Log out"},{UI_B,"Back"}};
            ui_footer(ui,hints,4);
        }
        ui_present(ui);ui_wait(ui);
    }
done:
    steam_secret_clear(password,sizeof(password));steam_secret_clear(account,sizeof(account));
    steam_secret_clear(challenge,sizeof(challenge));steam_secret_clear(last,sizeof(last));
    steam_secret_clear(qr,sizeof(qr));steam_secret_clear(temp,sizeof(temp));
    ui_start_screen(ui);
}

int steam_ui_download(struct steam_download_view *view)
{
    if(!provider)return 0;
    steam_provider_download_status(provider,view);return 1;
}
void steam_ui_stop(void)
{
    struct steam_download_view d;
    if(!provider)return;
    steam_provider_download_status(provider,&d);
    if(d.state==STEAM_DOWNLOAD_ERROR&&launcher_root[0])steam_download_failure_log(launcher_root,&d,"the user chose to cancel");
    steam_provider_cancel_download(provider);
}
int steam_ui_restart(void){return provider&&steam_provider_restart_download(provider);}
int steam_ui_resume(uint32_t app_id){return provider&&app_id&&steam_provider_install(provider,app_id);}
int steam_ui_online(void)
{
    struct steam_status_view v;
    if(!provider)return 0;
    steam_provider_status(provider,&v);return v.state==STEAM_ONLINE;
}
size_t steam_ui_owned(const char *root,struct steam_game *games,size_t capacity)
{
    struct steam_library_view l;size_t total=0;
    steam_ui_prepare(root);if(!provider)return 0;
    steam_provider_library_status(provider,&l);
    if(l.state!=STEAM_LIBRARY_READY)return 0;
    return steam_provider_library_page(provider,"",0,games,capacity,&total);
}
void steam_ui_game(struct ui *ui,const char *root,const struct steam_game *game)
{
    steam_ui_prepare(root);
    if(!provider){ui_message(ui,"Steam","Unable to create provider.");return;}
    steam_library_ui_game(ui,provider,root,game,&launcher);
}
/* The shortest way to being signed in: the saved session when there is one,
 * the sign-in screen (QR first) when there is not. */
void steam_ui_sign_in(struct ui *ui,const char *root)
{
    struct steam_session_view vault;struct steam_status_view v;
    steam_ui_prepare(root);
    if(!provider){ui_message(ui,"Steam","Unable to create provider.");return;}
    steam_provider_status(provider,&v);steam_provider_session_status(provider,&vault);
    if(v.state==STEAM_ONLINE){steam_ui_settings(ui,root);return;}
    if(vault.present)session_action(ui,0);
    else steam_ui_open(ui,root);
}
/* At start: a session saved for this console signs in by itself. */
void steam_ui_autoresume(const char *root)
{
    struct steam_session_view vault;struct steam_status_view v;
    steam_ui_prepare(root);if(!provider)return;
    steam_provider_status(provider,&v);steam_provider_session_status(provider,&vault);
    if(vault.present&&vault.automatic&&v.state==STEAM_OFFLINE)steam_provider_resume_session(provider);
}
