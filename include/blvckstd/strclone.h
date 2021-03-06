#ifndef strclone_h
#define strclone_h

#include <stddef.h>
#include <string.h>

#include "blvckstd/uminmax.h"
#include "blvckstd/strfmt.h"
#include "blvckstd/mman.h"

/**
 * @brief Clone a string safely by limiting it's length
 * 
 * @param origin Original string to be cloned
 * @param max_len Maximum length of the clone
 * @return char* Cloned string, NULL on errors
 */
char *strclone_s(const char *origin, size_t max_len);

/**
 * @brief Clone a string unsafely
 * 
 * @param origin Original string to be cloned
 * @return char* Cloned string, NULL on errors
 */
char *strclone(const char *origin);

#endif