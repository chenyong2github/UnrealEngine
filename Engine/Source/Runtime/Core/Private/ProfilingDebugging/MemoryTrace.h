// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Trace/Trace.h"

#if !defined(UE_MEMORY_TRACE_ENABLED) && UE_TRACE_ENABLED
#	if PLATFORM_WINDOWS
#		if !PLATFORM_USES_FIXED_GMalloc_CLASS && PLATFORM_64BITS
#			define UE_MEMORY_TRACE_ENABLED !UE_BUILD_SHIPPING
#		endif
#	endif
#endif

#if !defined(UE_MEMORY_TRACE_ENABLED)
#	define UE_MEMORY_TRACE_ENABLED 0
#endif

#if UE_MEMORY_TRACE_ENABLED

UE_TRACE_CHANNEL_EXTERN(MemTrackChannel);

class FMalloc*	MemoryTrace_Create(class FMalloc* InMalloc);
void			MemoryTrace_Initialize();
#if 0
CORE_API void	MemoryTrace_CoreAdd(void* Base, SIZE_T Size);
CORE_API void	MemoryTrace_CoreRemove(void* Base, SIZE_T Size);
#endif // 0

#else

#if 0
inline void		MemoryTrace_CoreAdd(void* Base, SIZE_T Size) {}
inline void		MemoryTrace_CoreRemove(void* Base, SIZE_T Size) {}
#endif // 0

#endif // UE_MEMORY_TRACE_ENABLED
