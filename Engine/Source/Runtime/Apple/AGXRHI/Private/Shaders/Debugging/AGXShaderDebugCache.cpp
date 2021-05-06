// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXShaderDebugCache.cpp: AGX RHI Shader Debug Cache.
=============================================================================*/

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "mtlpp.hpp"
THIRD_PARTY_INCLUDES_END

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include "AGXShaderDebugCache.h"
#include "AGXShaderDebugZipFile.h"

#if !UE_BUILD_SHIPPING

FAGXShaderDebugZipFile* FAGXShaderDebugCache::GetDebugFile(FString Path)
{
	FScopeLock Lock(&Mutex);
	FAGXShaderDebugZipFile* Ref = DebugFiles.FindRef(Path);
	if (!Ref)
	{
		Ref = new FAGXShaderDebugZipFile(Path);
		DebugFiles.Add(Path, Ref);
	}
	return Ref;
}

ns::String FAGXShaderDebugCache::GetShaderCode(uint32 ShaderSrcLen, uint32 ShaderSrcCRC)
{
	ns::String Code;
	FScopeLock Lock(&Mutex);
	for (auto const& Ref : DebugFiles)
	{
		Code = Ref.Value->GetShaderCode(ShaderSrcLen, ShaderSrcCRC);
		if (Code)
		{
			break;
		}
	}
	return Code;
}

#endif // !UE_BUILD_SHIPPING
