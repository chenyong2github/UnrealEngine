// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UStreamableSparseVolumeTexture;
class FRDGBuilder;

namespace UE
{
namespace SVT
{

// Interface for the SparseVolumeTexture streaming manager
class ENGINE_API IStreamingManager
{
public:
	//~ Begin game thread functions.
	virtual void Add_GameThread(UStreamableSparseVolumeTexture* SparseVolumeTexture) = 0;
	virtual void Remove_GameThread(UStreamableSparseVolumeTexture* SparseVolumeTexture) = 0;
	// Request a frame to be streamed in. FrameIndex is of float type so that the fractional part can be used to better track the playback speed/direction.
	// This function automatically also requests all higher mip levels and adds prefetch requests for upcoming frames.
	virtual void Request_GameThread(UStreamableSparseVolumeTexture* SparseVolumeTexture, float FrameIndex, int32 MipLevel) = 0;
	//~ End game thread functions.
	
	//~ Begin rendering thread functions.
	virtual void Request(UStreamableSparseVolumeTexture* SparseVolumeTexture, float FrameIndex, int32 MipLevel) = 0;
	virtual void BeginAsyncUpdate(FRDGBuilder& GraphBuilder) = 0;
	virtual void EndAsyncUpdate(FRDGBuilder& GraphBuilder) = 0;
	//~ End rendering thread functions.

	virtual ~IStreamingManager() = default;
};

ENGINE_API IStreamingManager& GetStreamingManager();

}
}
