#!/usr/bin/env python3
"""Generate exact versioned vtables from the pinned GBE compatibility headers.

The runtime is an intentionally local subset, not a full GBE distribution.
Unknown methods return their unavailable value; no networking worker is built.
"""
import argparse
import pathlib
import re

# The last line: families this library has nothing to say in. Every function of theirs
# answers "no" or nothing, but it is there to be called: a game that imports SteamUGC
# and found no such function was stopped by the loader's own stub the moment it asked.
FAMILIES = ('user', 'apps', 'utils', 'friends', 'userstats', 'remotestorage', 'client', 'input',
            'matchmaking', 'matchmakingservers', 'networking', 'gameserver', 'screenshots',
            'ugc', 'http', 'controller', 'inventory', 'music', 'musicremote', 'video', 'applist', 'htmlsurface', 'unifiedmessages', 'gameserverstats', 'parentalsettings', 'remoteplay')

def methods(text):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    for sig in re.findall(r'virtual\s+([^;{}]+?)\s*=\s*0\s*;', text, re.S):
        sig = ' '.join(sig.split())
        match = re.fullmatch(r'(.+?)\s+(\w+)\s*\((.*)\)\s*(const)?', sig)
        if not match:
            # Pointer return types often adjoin the method name.
            match = re.fullmatch(r'(.+?[*&])\s*(\w+)\s*\((.*)\)\s*(const)?', sig)
        if not match:
            raise ValueError('Cannot parse virtual method: ' + sig)
        ret, name, params, const = match.groups()
        params = params.replace('void (*)()', 'void (*unused_hook)()')
        args = []
        for p in params.split(',') if params.strip() else []:
            p = p.split('=')[0].strip()
            found = re.search(r'\(\*(\w+)\)', p) or re.search(r'(\w+)\s*(?:\[[^]]*\])?$', p)
            if not found:
                raise ValueError('Cannot parse parameter: ' + p)
            args.append(found.group(1))
        yield ret.strip(), name, params, args, const

def body(family, ret, name, args):
    a = args
    default = 'return {};' if ret != 'void' else 'return;'
    if ret.replace(' ','') == 'constchar*': default = 'return "";'
    if ret == 'EBeginAuthSessionResult': default = 'return k_EBeginAuthSessionResultInvalidTicket;'
    if ret == 'EUserHasLicenseForAppResult': default = 'return k_EUserHasLicenseResultDoesNotHaveLicense;'
    if family == 'client':
        if name == 'CreateSteamPipe': return 'return ready ? 1 : 0;'
        if name == 'ConnectToGlobalUser': return 'return ready && ' + a[0] + ' == 1 ? 1 : 0;'
        if name == 'BReleaseSteamPipe': return 'return ' + a[0] + ' == 1;'
        if name.startswith('GetISteam') and a and 'char' in ret:
            return default
        if name.startswith('GetISteam') and a:
            return 'return static_cast<' + ret + '>(lookup(' + a[-1] + '));'
    if family == 'user':
        if name == 'GetHSteamUser': return 'return ready ? 1 : 0;'
        if name == 'GetSteamID': return 'return account;' if ret == 'uint64_steamid' else 'return CSteamID(account);'
        if name == 'BLoggedOn': return 'return false; /* Offline session. */'
        if name == 'GetUserDataFolder': return 'return user_data_folder(' + ','.join(a) + ');'
        if name == 'UserHasLicenseForApp': return 'return owns_license('+','.join(a)+');'
    if family == 'apps':
        if name == 'BIsSubscribed': return 'return ready;'
        if name in ('BIsSubscribedApp', 'BIsAppInstalled'):
            return 'return ready && ' + a[0] + ' == appid;'
        if name in ('GetCurrentGameLanguage', 'GetAvailableGameLanguages'): return 'return game_language;'
        if name == 'GetAppOwner': return 'return account;' if ret == 'uint64_steamid' else 'return CSteamID(account);'
        if name == 'GetAppBuildId': return 'return 0;'
    if family == 'friends':
        if name == 'GetPersonaName': return 'return "Autorun";'
        if name == 'GetPersonaState': return 'return k_EPersonaStateOffline;'
    if family == 'utils':
        if name == 'GetAppID': return 'return appid;'
        if name == 'GetConnectedUniverse': return 'return k_EUniversePublic;'
        if name == 'GetIPCountry': return 'return "";'
        if name == 'GetCurrentBatteryPower': return 'return 255;'
        if name == 'GetSteamUILanguage': return 'return ui_language;'
        if name == 'GetServerRealTime': return 'return (uint32)time(nullptr);'
        if name == 'IsAPICallCompleted': return 'return result_complete(' + ','.join(a) + ');'
        if name == 'GetAPICallResult': return 'return result_copy(' + ','.join(a) + ');'
        if name == 'GetAPICallFailureReason': return 'return result_failure(' + ','.join(a) + ');'
    if family == 'remotestorage':
        mapping = {'FileWrite':'save_write', 'FileRead':'save_read', 'FileExists':'save_exists',
                   'FileDelete':'save_delete', 'GetFileSize':'save_size', 'GetFileCount':'save_count',
                   'GetFileNameAndSize':'save_name', 'GetFileTimestamp':'save_time', 'GetQuota':'save_quota'}
        if name in mapping: return 'return ' + mapping[name] + '(' + ','.join(a) + ');'
        if name in ('IsCloudEnabledForAccount', 'IsCloudEnabledForApp', 'FilePersisted'): return 'return false;'
        if name == 'GetSyncPlatforms': return 'return k_ERemoteStoragePlatformNone;'
    if family == 'userstats':
        if name == 'RequestCurrentStats': return 'return request_stats();'
        if name == 'GetAchievement' and len(a) == 2:
            return 'if ('+a[1]+') *'+a[1]+' = false; return false;'
    if family == 'screenshots':
        if name == 'HookScreenshots': return 'Guard guard; screenshots_hooked = '+a[0]+';'
        if name == 'IsScreenshotsHooked': return 'Guard guard; return screenshots_hooked;'
    return default

# A game built with Visual C++ calls these interfaces by Visual C++'s rules, and this
# library is built with a compiler that follows GCC's. They differ in two places.
#
# A member function that returns a small class by value (a CSteamID is eight bytes)
# is, for Visual C++, given the address to write it to as a hidden first argument
# and returns that address; GCC's rules return the value itself in registers. A game
# asking "who is the user" then took the account's number for an address and read
# from it: Age of Empires II HD stopped with an access violation five seconds in.
#
# Functions of one name declared one after another are, for Visual C++, in the table
# in the reverse of the order they are declared in.
#
# So each interface gets a table of its own, laid out as Visual C++ would, and each
# function returning such a class gets a small function in front of it that takes
# and returns the hidden address.
SMALL_CLASSES = ('CSteamID', 'CGameID', 'InputDigitalActionData_t')

def msvc_order(found):
    order, seen = [], set()
    for i, method in enumerate(found):
        name = method[1]
        if name in seen: continue
        seen.add(name)
        order += reversed([j for j, other in enumerate(found) if other[1] == name])
    return order

def abi_table(base, cls, found):
    lines, slots = [], []
    for i, (ret, name, params, args, const) in enumerate(found):
        if ret in SMALL_CLASSES:
            thunk = 'abi_%s_%d' % (base, i)
            lines.append('static %s *ABI_THISCALL %s(%s *self, %s *result%s) { *result = self->%s::%s(%s); return result; }' % (
                ret, thunk, cls, ret, (', ' + params) if params.strip() else '', cls, name, ', '.join(args)))
            slots.append('(void *)' + thunk)
        else:
            slots.append('original[%d]' % i)
    lines.append('static void *abi_table_%s[%d];' % (base, len(found)))
    lines.append('static void abi_fix_%s() { void **original = *(void ***)&instance_%s;' % (base, base))
    for slot, i in enumerate(msvc_order(found)):
        lines.append('  abi_table_%s[%d] = %s;' % (base, slot, slots[i]))
    lines.append('  *(void ***)&instance_%s = abi_table_%s; }' % (base, base))
    return lines

def generate(source, output, preprocessed=None):
    includes, classes, lookups, fixes = [], [], [], []
    totals = 0
    for family in FAMILIES:
        paths = sorted((source / 'sdk/steam').glob('isteam' + family + '*.h'))
        # Avoid interfaces whose filename shares a prefix, such as UserItems.
        paths = [p for p in paths if re.fullmatch('isteam'+family+r'\d*\.h', p.name)]
        for p in paths:
            text = p.read_text(errors='replace')
            camel = {'userstats':'UserStats','remotestorage':'RemoteStorage','gameserver':'GameServer',
                     'matchmaking':'Matchmaking','matchmakingservers':'MatchmakingServers',
                     'ugc':'UGC','http':'HTTP','controller':'Controller','inventory':'Inventory','music':'Music','musicremote':'MusicRemote','video':'Video','applist':'AppList','htmlsurface':'HTMLSurface','unifiedmessages':'UnifiedMessages','gameserverstats':'GameServerStats','parentalsettings':'ParentalSettings','remoteplay':'RemotePlay'}.get(family,family.title())
            suffix = re.search(r'(\d+)\.h$', p.name)
            if suffix and int(suffix.group(1)) < {'user':8,'client':7,'friends':4}.get(family,1):
                continue
            expected = 'ISteam'+camel+(suffix.group(1) if suffix else '')
            effective = preprocessed if preprocessed is not None else text
            match = re.search(r'class\s+('+expected+r')\s*\{', effective)
            if not match: continue
            base = match.group(1)
            version = re.search(r'#define\s+\w+_INTERFACE_VERSION\s+"([^"]+)"', text)
            if not version:
                suffix = re.search(r'(\d+)$', base)
                if not suffix: raise ValueError('No interface version: ' + str(p))
                prefix = {'apps':'STEAMAPPS_INTERFACE_VERSION','gameserver':'SteamGameServer',
                          'matchmaking':'SteamMatchMaking','matchmakingservers':'SteamMatchMakingServers',
                          'remotestorage':'STEAMREMOTESTORAGE_INTERFACE_VERSION',
                          'userstats':'STEAMUSERSTATS_INTERFACE_VERSION',
                          'screenshots':'STEAMSCREENSHOTS_INTERFACE_VERSION',
                          'ugc':'STEAMUGC_INTERFACE_VERSION','http':'STEAMHTTP_INTERFACE_VERSION','controller':'SteamController','inventory':'STEAMINVENTORY_INTERFACE_V','music':'STEAMMUSIC_INTERFACE_VERSION','musicremote':'STEAMMUSICREMOTE_INTERFACE_VERSION','video':'STEAMVIDEO_INTERFACE_V','applist':'STEAMAPPLIST_INTERFACE_VERSION','htmlsurface':'STEAMHTMLSURFACE_INTERFACE_VERSION_','unifiedmessages':'STEAMUNIFIEDMESSAGES_INTERFACE_VERSION','gameserverstats':'SteamGameServerStats','parentalsettings':'STEAMPARENTALSETTINGS_INTERFACE_VERSION','remoteplay':'STEAMREMOTEPLAY_INTERFACE_VERSION'}.get(family, 'Steam'+family.title())
                version_name = prefix + suffix.group(1)
            else: version_name = version.group(1)
            includes.append('#include "steam/' + p.name + '"')
            cls = 'Local_' + base
            lines = ['struct '+cls+' final : '+base+' {']
            class_text = effective[match.end():effective.index('};',match.end())]
            found = list(methods(class_text))
            for ret, name, params, args, const in found:
                lines.append('  '+ret+' '+name+'('+params+')'+(' const' if const else '')+' override { '+body(family,ret,name,args)+' }')
                totals += 1
            lines += ['};', 'static '+cls+' instance_'+base+';']
            lines += abi_table(base, cls, found)
            fixes.append('  abi_fix_'+base+'();')
            classes += lines
            lookups.append('if (!strcmp(version,"'+version_name+'")) return static_cast<'+base+'*>(&instance_'+base+');')
    output.mkdir(parents=True, exist_ok=True)
    classes += ['static void abi_fix_all() {'] + fixes + ['}']
    (output/'interfaces.inc').write_text('\n'.join(classes)+'\n')
    (output/'interface_headers.inc').write_text('\n'.join(includes)+'\n')
    (output/'lookup.inc').write_text('\n'.join(lookups)+'\nreturn nullptr;\n')
    flat = (source/'sdk/steam/steam_api_flat.h').read_text(errors='replace')
    flat = re.sub(r'/\*.*?\*/|//[^\n]*','',flat,flags=re.S)
    wrappers = ['typedef uint64 uint64_steamid; typedef uint64 uint64_gameid;']
    for ret, name, params in re.findall(r'STEAMAPI_API\s+(.+?)\s+(\w+)\s*\(([^;]*)\)\s*;',flat):
        ret, params = ret.strip(), ' '.join(params.split())
        factory = re.fullmatch(r'SteamAPI_Steam(\w+)_v(\d+)',name)
        if factory and factory.group(1).lower() in FAMILIES:
            fam = factory.group(1).lower()
            prefix = {'apps':'STEAMAPPS_INTERFACE_VERSION','userstats':'STEAMUSERSTATS_INTERFACE_VERSION', 'remotestorage':'STEAMREMOTESTORAGE_INTERFACE_VERSION', 'screenshots':'STEAMSCREENSHOTS_INTERFACE_VERSION'}.get(fam,'Steam'+factory.group(1))
            wrappers.append('S_API_EXPORT '+ret+' '+name+'('+params+'){ return ('+ret+')lookup("'+prefix+factory.group(2)+'"); }')
            continue
        method = re.fullmatch(r'SteamAPI_ISteam(\w+?)_(\w+)',name)
        if not method or method.group(1).lower() not in FAMILIES: continue
        parts = [p.strip() for p in params.split(',')]
        names = [re.search(r'(\w+)\s*(?:\[[^]]*\])?$',p).group(1) for p in parts]
        default = 'return {};' if ret!='void' else 'return;'
        code = body(method.group(1).lower(),ret,method.group(2),names[1:])
        wrappers.append('S_API_EXPORT '+ret+' '+name+'('+params+'){ if (!ready || !'+names[0]+') { '+default+' } '+code+' }')
    (output/'flat.inc').write_text('\n'.join(wrappers)+'\n')
    print(f'Generated {len(lookups)} exact interfaces, {totals} method slots')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--preprocessed', type=pathlib.Path)
    opts = parser.parse_args()
    generate(opts.source, opts.output, opts.preprocessed.read_text(errors='replace') if opts.preprocessed else None)
