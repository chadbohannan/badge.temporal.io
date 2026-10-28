/* Force-included into sqlite3.c (see library.json).
**
** SQLite compiles its debug-only helper functions out under NDEBUG and expects
** assert() to vanish. ESP-IDF's <assert.h> (CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE)
** instead turns assert(e) into ((void)(e)), so the calls to those missing helpers
** still compile and fail. That header is #pragma once, so defining assert here,
** after the first include, holds when sqlite3.c includes <assert.h> itself.
*/
#include <assert.h>
#undef assert
#define assert(e) ((void)0)
