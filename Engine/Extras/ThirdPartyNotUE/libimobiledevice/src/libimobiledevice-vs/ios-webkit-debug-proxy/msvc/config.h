#if defined(_MSC_VER)

// Define ssize_t (as SSIZE_T)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

// The 'bool' type is used by various files, so include it.
#include <stdbool.h>

// Map string functions to their VC++ equivalent.
#ifndef strncasecmp
#define strncasecmp strnicmp
#endif

#ifndef strcasecmp
#define strcasecmp stricmp
#endif

#define PACKAGE_STRING "ios_webkit_debug_proxy"
#define PACKAGE_VERSION "libimobiledevice-win32"
#define LIBIMOBILEDEVICE_VERSION "libimobiledevice-win32"
#define LIBPLIST_VERSION "libimobiledevice-win32"

#endif 
