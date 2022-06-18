// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCompiledShaderCache.h: AGX RHI Compiled Shader Cache.
=============================================================================*/

#pragma once

#include "Misc/ScopeRWLock.h"

struct FAGXCompiledShaderCache
{
	FAGXCompiledShaderCache()
	{
		// VOID
	}

	~FAGXCompiledShaderCache()
	{
		for (auto& Entry : Cache)
		{
			[Entry.Value release];
		}
	}

	id<MTLFunction> FindRef(const FAGXCompiledShaderKey& Key)
	{
		FRWScopeLock ScopedLock(Lock, SLT_ReadOnly);
		return Cache.FindRef(Key);
	}

	TRefCountPtr<FMTLLibrary> FindLibrary(id<MTLFunction> Function)
	{
		FRWScopeLock ScopedLock(Lock, SLT_ReadOnly);
		return LibCache.FindRef(Function);
	}

	void Add(FAGXCompiledShaderKey Key, const TRefCountPtr<FMTLLibrary>& Library, id<MTLFunction> Function)
	{
		FRWScopeLock ScopedLock(Lock, SLT_Write);
		if (Cache.FindRef(Key) == nil)
		{
			Cache.Add(Key, Function);
			LibCache.Add(Function, Library);
		}
	}

private:
	FRWLock Lock;
	TMap<FAGXCompiledShaderKey, id<MTLFunction>> Cache;
	TMap<id<MTLFunction>, TRefCountPtr<FMTLLibrary>> LibCache;
};

extern FAGXCompiledShaderCache& GetAGXCompiledShaderCache();
