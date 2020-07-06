// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

class FVirtualTextureChunkDDCCache
{
public:
	void Initialize();
	void ShutDown();
	void UpdateRequests();

	bool MakeChunkAvailable(struct FVirtualTextureDataChunk* Chunk, bool bAsync, FString& OutChunkFileName, int64& OutOffsetInFile);

private:
	TArray<struct FVirtualTextureDataChunk*> ActiveChunks;
	FString AbsoluteCachePath;
};

FVirtualTextureChunkDDCCache* GetVirtualTextureChunkDDCCache();

#endif