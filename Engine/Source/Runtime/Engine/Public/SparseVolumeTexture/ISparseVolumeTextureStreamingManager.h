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
	virtual void Request_GameThread(UStreamableSparseVolumeTexture* SparseVolumeTexture, float FrameIndex, int32 MipLevel, bool bBlocking) = 0;
	// Issues a rendering command for updating the streaming manager. This is not normally needed, but may be necessary in cases where blocking requests are required
	// and the SVT is used in a different rendering command that is executed before the streaming manager would normally update.
	virtual void Update_GameThread() = 0;
	//~ End game thread functions.
	
	//~ Begin rendering thread functions.
	virtual void Request(UStreamableSparseVolumeTexture* SparseVolumeTexture, float FrameIndex, int32 MipLevel, bool bBlocking) = 0;
	virtual void BeginAsyncUpdate(FRDGBuilder& GraphBuilder, bool bForceNonAsync = false) = 0;
	virtual void EndAsyncUpdate(FRDGBuilder& GraphBuilder) = 0;
	//~ End rendering thread functions.

	virtual ~IStreamingManager() = default;
};

ENGINE_API IStreamingManager& GetStreamingManager();

}
}
