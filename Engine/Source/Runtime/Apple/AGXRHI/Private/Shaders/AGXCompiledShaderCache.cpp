// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCompiledShaderCache.cpp: AGX RHI Compiled Shader Cache.
=============================================================================*/

#include "CoreMinimal.h"
#include "AGXRHIPrivate.h"
#include "AGXCompiledShaderKey.h"
#include "AGXCompiledShaderCache.h"

FAGXCompiledShaderCache& GetAGXCompiledShaderCache()
{
	static FAGXCompiledShaderCache CompiledShaderCache;
	return CompiledShaderCache;
}
