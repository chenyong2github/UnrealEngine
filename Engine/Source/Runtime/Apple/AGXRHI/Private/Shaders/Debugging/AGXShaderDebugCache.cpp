// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXShaderDebugCache.cpp: AGX RHI Shader Debug Cache.
=============================================================================*/

#include "CoreMinimal.h"

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

NSString* FAGXShaderDebugCache::GetShaderCode(uint32 ShaderSrcLen, uint32 ShaderSrcCRC)
{
	FScopeLock Lock(&Mutex);
	for (auto const& Ref : DebugFiles)
	{
		NSString* Code = Ref.Value->GetShaderCode(ShaderSrcLen, ShaderSrcCRC);
		if (Code != nil)
		{
			return Code;
		}
	}
	return nil;
}

#endif // !UE_BUILD_SHIPPING
