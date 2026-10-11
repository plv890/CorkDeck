/* Factual local SDK provider registration in Autorun's owned Wine prefix.
 * Caller must opt in and verify the selected DLL hash/architecture/config first.
 * Uses real NT registry objects; never advertises an account or auth state.
 * The process recorded as the running client is the program's own: Steam's
 * library takes a client for running only when that process is alive.
 * Invoke after PEB/environment initialization, before guest application entry. */
#ifndef AUTORUN_STEAM_LEGACY_REGISTRY_H
#define AUTORUN_STEAM_LEGACY_REGISTRY_H

enum steam_legacy_registry_result {
    STEAM_LEGACY_REGISTRY_READY,
    STEAM_LEGACY_REGISTRY_CONFLICT,
    STEAM_LEGACY_REGISTRY_BAD_PATH,
    STEAM_LEGACY_REGISTRY_ERROR
};

struct steam_legacy_value_info {
    ULONG title, type, bytes;
    WCHAR data[1024];
};

static void steam_legacy_registry_name( UNICODE_STRING *out, const WCHAR *text )
{
    unsigned int chars=0;
    while (text[chars]) ++chars;
    out->Buffer=(WCHAR *)text;
    out->Length=chars*sizeof(WCHAR);
    out->MaximumLength=out->Length+sizeof(WCHAR);
}

static int steam_legacy_registry_path_valid( const WCHAR *path, unsigned int *chars )
{
    static const char basename[]="steamclient-local.dll";
    unsigned int n=0, base=0, i;
    if (!path) return 0;
    while (n<1024 && path[n]) ++n;
    if (n<3 || n>=1024 || path[1]!=':' || path[2]!='\\' ||
        !((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))) return 0;
    for (i=2;i<n;++i)
    {
        if (path[i]=='\\') base=i+1;
        else if (path[i]<' ' || path[i]=='/' || path[i]==':' || path[i]=='*' || path[i]=='?') return 0;
    }
    for (i=0;basename[i];++i)
    {
        WCHAR c=path[base+i];
        if(c>='A'&&c<='Z')c+='a'-'A';
        if(c!=(unsigned char)basename[i])return 0;
    }
    if (base+i!=n) return 0;
    *chars=n;
    return 1;
}

static int steam_legacy_registry_register( const WCHAR *verified_path, NTSTATUS *failure )
{
    /* The prefix has one user; this is the key HKEY_CURRENT_USER resolves to.
     * Every level is opened by its absolute name, as the runtime's other
     * start-up key is, so no PE-side helper is needed before guest entry. */
    static const char full[]="\\Registry\\User\\S-1-5-21-0-0-0-1000\\Software\\Valve\\Steam\\ActiveProcess";
    static const WCHAR value_name[]={'S','t','e','a','m','C','l','i','e','n','t','D','l','l',0};
    static const unsigned int first=sizeof("\\Registry\\User\\S-1-5-21-0-0-0-1000")-1;
    WCHAR path[sizeof(full)];
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attr;
    HANDLE key=NULL,next=NULL;
    struct steam_legacy_value_info value;
    NTSTATUS status=0;
    ULONG bytes=0;
    unsigned int chars,i;
    int result=STEAM_LEGACY_REGISTRY_ERROR;
    if (failure) *failure=0;
    if (!steam_legacy_registry_path_valid(verified_path,&chars)) return STEAM_LEGACY_REGISTRY_BAD_PATH;
    for(i=0;i<sizeof(full);++i)path[i]=(unsigned char)full[i];
    for(i=first;i<sizeof(full);++i)
    {
        WCHAR end=path[i];
        if(end && end!='\\')continue;
        path[i]=0;
        name.Buffer=path;name.Length=i*sizeof(WCHAR);name.MaximumLength=sizeof(path);
        InitializeObjectAttributes(&attr,&name,OBJ_CASE_INSENSITIVE,NULL,NULL);
        status=NtCreateKey(&next,KEY_CREATE_SUB_KEY|KEY_QUERY_VALUE|KEY_SET_VALUE,&attr,0,NULL,REG_OPTION_NON_VOLATILE,NULL);
        path[i]=end;
        if(status)goto done;
        if(key)NtClose(key);
        key=next;next=NULL;
    }
    steam_legacy_registry_name(&name,value_name);
    status=NtQueryValueKey(key,&name,(KEY_VALUE_INFORMATION_CLASS)2,&value,sizeof(value),&bytes);
    if (!status)
    {
        /* Exact selected path only. File integrity is revalidated by the caller;
         * equal registry text alone never establishes provider ownership. */
        if(value.type!=REG_SZ || value.bytes!=(chars+1)*sizeof(WCHAR) ||
           bytes<12+value.bytes || value.data[chars]!=0)
        { result=STEAM_LEGACY_REGISTRY_CONFLICT;goto done; }
        for(i=0;i<=chars;++i) if(value.data[i]!=verified_path[i])
        { result=STEAM_LEGACY_REGISTRY_CONFLICT;goto done; }
        result=STEAM_LEGACY_REGISTRY_READY;goto done;
    }
    /* Missing value is the only condition allowing creation. A too-large,
     * unreadable, differently typed or unknown existing value is preserved. */
    if(status!=(NTSTATUS)0xc0000034)
    { result=STEAM_LEGACY_REGISTRY_CONFLICT;goto done; }
    status=NtSetValueKey(key,&name,0,REG_SZ,(void *)verified_path,(chars+1)*sizeof(WCHAR));
    if(!status)result=STEAM_LEGACY_REGISTRY_READY;
done:
    if(key && result==STEAM_LEGACY_REGISTRY_READY)
    {
        /* SteamAPI_Init reads "pid" first and opens that process; without a live
         * one it reports that no Steam is running and never loads the library. */
        static const WCHAR pid_name[]={'p','i','d',0};
        DWORD pid=GetCurrentProcessId();
        steam_legacy_registry_name(&name,pid_name);
        status=NtSetValueKey(key,&name,0,REG_DWORD,&pid,sizeof(pid));
        if(status)result=STEAM_LEGACY_REGISTRY_ERROR;
    }
    if(key)NtClose(key);
    if(next)NtClose(next);
    if(failure && result==STEAM_LEGACY_REGISTRY_ERROR)*failure=status;
    return result;
}
#endif
