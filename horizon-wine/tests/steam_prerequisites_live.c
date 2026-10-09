/* Optional official-origin integration check. No Steam login or credentials. */
#include "../source/steam/steam_prerequisites.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv){assert(argc==2);for(unsigned int arch=0;arch<2;++arch){unsigned int e=steam_prereq_fetch(argv[1],arch,NULL);printf("official installer arch=%u error=%u pinned_sha256=%s\n",arch,e,steam_prereq_catalog(arch)->sha256);assert(!e&&steam_prereq_cached(argv[1],arch));char target[512];assert(!steam_prereq_prepare(argv[1],arch,0,target));assert(!steam_prereq_result(argv[1],arch));printf("Prepared verified ordinary installer with private arguments: %s\n",target);}return 0;}
