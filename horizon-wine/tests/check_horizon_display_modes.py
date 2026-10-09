"""Execute the actual virtual mode generator; no game or graphics driver."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'dlls/win32u/sysparams.c').read_text()
def block(signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
functions='\n'.join(block(s) for s in ('static UINT devmode_get(', 'static UINT add_screen_size(',
    'static UINT add_virtual_mode(', 'static SIZE *get_screen_sizes(', 'static DEVMODEW *get_virtual_modes('))
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#define TRACE(...) ((void)0)
#define NEXT_DEVMODEW(m) ((DEVMODEW*)((char*)((m)+1)+(m)->dmDriverExtra))
'''+functions+r'''
int main(void){
    DEVMODEW initial={.dmSize=sizeof(initial),.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL|DM_DISPLAYFREQUENCY,
        .dmPelsWidth=1280,.dmPelsHeight=720,.dmBitsPerPel=32,.dmDisplayFrequency=60};
    DEVMODEW host=initial,maximum=initial;
    maximum.dmPelsWidth=1920;maximum.dmPelsHeight=1080;
    host.dmPelsWidth=3840;host.dmPelsHeight=2160;
    UINT count;DEVMODEW*m=get_virtual_modes(&initial,&maximum,&host,1,&count);
    assert(m&&count);int lower=0,current=0;
    for(UINT i=0;i<count;i++){
        assert(m[i].dmPelsWidth<=1280&&m[i].dmPelsHeight<=720);
        lower|=m[i].dmPelsWidth==960&&m[i].dmPelsHeight==540;
        current|=m[i].dmPelsWidth==1280&&m[i].dmPelsHeight==720;
    }
    assert(lower&&current);free(m);
    initial.dmPelsWidth=1920;initial.dmPelsHeight=1080;
    m=get_virtual_modes(&initial,&maximum,&host,1,&count);
    assert(m&&count);
    for(UINT i=0;i<count;i++)assert(m[i].dmPelsWidth<=1280&&m[i].dmPelsHeight<=720);
    free(m);
    puts("Horizon display modes: cap, lower modes, oversized host and initial modes passed");
}
'''
with tempfile.TemporaryDirectory(prefix='horizon-display-') as folder:
    p=Path(folder);(p/'display.c').write_text(fixture)
    subprocess.run(['clang','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-no-pie','-D__SWITCH__','-D__WINESRC__','-D_WIN64','-I'+str(root/'include'),str(p/'display.c'),'-o',str(p/'display')],check=True)
    subprocess.run([str(p/'display')],check=True)
