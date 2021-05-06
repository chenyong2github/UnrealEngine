// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXShaderDebugCache.h: AGX RHI Shader Debug Cache.
=============================================================================*/

#pragma once

#if !UE_BUILD_SHIPPING

struct FAGXShaderDebugCache
{
	static FAGXShaderDebugCache& Get()
	{
		static FAGXShaderDebugCache sSelf;
		return sSelf;
	}
	
	class FAGXShaderDebugZipFile* GetDebugFile(FString Path);
	ns::String GetShaderCode(uint32 ShaderSrcLen, uint32 ShaderSrcCRC);
	
	FCriticalSection Mutex;
	TMap<FString, class FAGXShaderDebugZipFile*> DebugFiles;
};

#endif // !UE_BUILD_SHIPPING
