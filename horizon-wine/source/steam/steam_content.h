#ifndef AUTORUN_STEAM_CONTENT_H
#define AUTORUN_STEAM_CONTENT_H
#include <stddef.h>
#include <stdint.h>
#define STEAM_CHUNK_LIMIT (4u*1024u*1024u)
enum steam_integrity_detail { STEAM_INTEGRITY_NONE, STEAM_INTEGRITY_RESPONSE_SIZE,
    STEAM_INTEGRITY_DECRYPT, STEAM_INTEGRITY_COMPRESSION, STEAM_INTEGRITY_CHECKSUM,
    STEAM_INTEGRITY_CHUNK_SHA, STEAM_INTEGRITY_FILE_SHA, STEAM_INTEGRITY_MANIFEST,
    STEAM_INTEGRITY_LAYOUT, STEAM_INTEGRITY_SIZE, STEAM_INTEGRITY_MEMORY };
/* Format: 0 unknown, 1 ZIP, 2 VZip/LZMA, 3 VZstd. No payload/secret logging. */
unsigned int steam_content_chunk_check(const unsigned char key[32],const void *,size_t,unsigned char *,size_t,uint32_t,const unsigned char sha[20],unsigned int *format);
int steam_content_decrypt(const unsigned char key[32],const void *,size_t,unsigned char *,size_t,size_t *);
int steam_content_chunk(const unsigned char key[32],const void *,size_t,unsigned char *,size_t,uint32_t checksum,const unsigned char sha[20]);
int steam_content_filename(const unsigned char key[32],const void *,size_t,char out[384]);
int steam_content_path(char *); /* normalize separators and reject escaping/ambiguous paths */
#endif
