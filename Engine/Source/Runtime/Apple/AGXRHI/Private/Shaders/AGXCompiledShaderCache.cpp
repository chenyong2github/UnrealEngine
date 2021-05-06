// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCompiledShaderCache.cpp: AGX RHI Compiled Shader Cache.
=============================================================================*/

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "mtlpp.hpp"
THIRD_PARTY_INCLUDES_END

#include "AGXCompiledShaderKey.h"
#include "AGXCompiledShaderCache.h"

FAGXCompiledShaderCache& GetAGXCompiledShaderCache()
{
	static FAGXCompiledShaderCache CompiledShaderCache;
	return CompiledShaderCache;
}
