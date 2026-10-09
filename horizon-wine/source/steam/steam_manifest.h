#ifndef AUTORUN_STEAM_MANIFEST_H
#define AUTORUN_STEAM_MANIFEST_H
#include <stdio.h>
#include "steam_wire.h"
#include "steam_content.h"
struct steam_chunk { unsigned char sha[20];uint64_t offset;uint32_t checksum,original,compressed; };
struct steam_manifest_file { char path[384];uint64_t size;uint32_t flags;unsigned char sha[20];
    struct steam_wire_reader chunks; };
struct steam_manifest { FILE *file;uint64_t payload_offset,payload_size,remaining,total_size,manifest;uint32_t depot;int encrypted;
    unsigned char *mapping;size_t mapping_size;unsigned char key[32]; };
int steam_manifest_open(struct steam_manifest *,const char *,uint32_t depot,uint64_t manifest,const unsigned char key[32]);
void steam_manifest_close(struct steam_manifest *);
/* 1=file, 0=end, -1=corrupt/unsupported. One file mapping in memory at a time. */
int steam_manifest_next(struct steam_manifest *,struct steam_manifest_file *);
int steam_manifest_chunk(struct steam_manifest_file *,struct steam_chunk *);
#endif
