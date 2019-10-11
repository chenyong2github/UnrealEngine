#pragma once

#if defined(__APPLE__)
#   include <TargetConditionals.h>
#endif

#ifndef VX_PLATFORM_NAME
#  if defined(WIN32_ONLY)
#    define VX_PLATFORM_NAME "MSWin32"
#  elif defined(_UAP)
#    define VX_PLATFORM_NAME "UWP"
#  elif defined(__APPLE__) && !TARGET_OS_IPHONE
#    define VX_PLATFORM_NAME "darwin"
#  elif defined(__ANDROID__)
#    define VX_PLATFORM_NAME "android"
#  elif defined(VXP_LINUX)
#    define VX_PLATFORM_NAME "linux"
#  elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#    define VX_PLATFORM_NAME "iphone"
#  else
#    error "Undefined Platform"
#  endif
#endif

#if defined(_UAP)
#   include <winsock2.h>
#elif defined(WIN32_ONLY)
#   include <winsock.h>
#endif

#if defined(WIN32_ONLY) || defined(_UAP)
#   include <windows.h>
#   pragma warning( push )
#   pragma warning( disable : 4091 )
#   include <shlobj.h>
#   pragma warning( pop )
#endif

#ifdef __APPLE__
#   define HAVE_PTHREAD_H 1
#   define HAVE_SEMAPHORE_H 1
#   define HAVE_GETADDRINFO 1
#   define HAVE_FCNTL_H 1
#   define HAVE_STRUCT_TIMEVAL 1
#   define HAVE_CTYPE_H 1
#   define HAVE_EXECINFO_H 1
#   define HAVE_NETDB_H 1
#   define HAVE_NETINET_IN_H 1
#   define HAVE_NET_IF_H 1
#   define HAVE_SYS_IOCTL_H 1
#   define HAVE_SYS_SOCKET_H 1
#   define HAVE_UNISTD_H 1
#   define HAVE_ATOMIC 1
#endif

#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif

#ifdef HAVE_ERRNO_H
#  include <errno.h>
#endif

#ifdef HAVE_EXECINFO_H
#  include <execinfo.h>
#endif

#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif

#ifdef HAVE_NET_IF_H
#  include <net/if.h>
#endif

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#ifdef HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#ifdef HAVE_SIGNAL_H
#  include <signal.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif

#ifdef HAVE_SYS_SYSCTL_H
#  include <sys/sysctl.h>
#endif

#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#if !defined(WIN32)
#   define SOCKET int
#   define INVALID_SOCKET (-1)
#   undef closesocket
#   define closesocket(x) close(x)
#   define WSAGetLastError() errno
#   define socket_t int
#   define socklen_t size_t
#else
#   define socket_t SOCKET
#   define socklen_t int
#endif

#if defined(_WIN32)
#   define HAVE_GETHOSTBYNAME 1
#endif

#if defined(WIN32) && !defined(strcasecmp)
#  define strcasecmp stricmp
#endif

#include <inttypes.h>

#if defined(_WIN64) || defined(__LP64__)
#    define PRIADDR "0x%016" PRIxPTR
#else
#    define PRIADDR "0x%08" PRIxPTR
#endif

#ifdef _UAP
inline char *getenv(const char *name)
{
    (void)name;
    return NULL;
}
#endif
