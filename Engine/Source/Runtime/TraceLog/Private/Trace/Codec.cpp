// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"

////////////////////////////////////////////////////////////////////////////////
#if defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable : 6239)
#endif

#include "ThirdParty/LZ4/lz4.c.h"

#if defined(_MSC_VER)
#	pragma warning(pop)
#endif

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
int32 Encode(const void* Src, int32 SrcSize, void* Dest, int32 DestSize)
{
	return LZ4_compress_fast(
		(const char*)Src,
		(char*)Dest,
		SrcSize,
		DestSize,
		1 // increase by 1 for small speed increase
	);
}

////////////////////////////////////////////////////////////////////////////////
TRACELOG_API int32 Decode(const void* Src, int32 SrcSize, void* Dest, int32 DestSize)
{
	return LZ4_decompress_safe((const char*)Src, (char*)Dest, SrcSize, DestSize);
}

} // namespace Private
} // namespace Trace
