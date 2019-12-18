// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DMipDataProvider_IO.h : Implementation of FTextureMipDataProvider using cooked file IO.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Streaming/TextureMipDataProvider.h"
#include "Async/AsyncFileHandle.h"

/**
* FTexture2DMipDataProvider_IO implements FTextureMipAllocator using file IO (cooked content).
* It support having mips stored in different files contrary to FTexture2DStreamIn_IO.
*/
class FTexture2DMipDataProvider_IO : public FTextureMipDataProvider
{
public:

	FTexture2DMipDataProvider_IO(bool InPrioritizedIORequest);
	~FTexture2DMipDataProvider_IO();

	// ********************************************************
	// ********* FTextureMipDataProvider implementation **********
	// ********************************************************

	void Init(const FTextureUpdateContext& Context, const FTextureUpdateSyncOptions& SyncOptions) final override;
	int32 GetMips(const FTextureUpdateContext& Context, int32 StartingMipIndex, const FTextureMipInfoArray& MipInfos, const FTextureUpdateSyncOptions& SyncOptions) final override;
	bool PollMips(const FTextureUpdateSyncOptions& SyncOptions) final override;
	void AbortPollMips() final override;
	void CleanUp(const FTextureUpdateSyncOptions& SyncOptions) final override;
	void Cancel(const FTextureUpdateSyncOptions& SyncOptions) final override;
	ETickThread GetCancelThread() const final override;

protected:

	// A structured with information about which file contains which mips.
	struct FFileInfo
	{
		FString IOFilename;
		TUniquePtr<IAsyncReadFileHandle> IOFileHandle;
		int64 IOFileOffset = 0;
		int32 FirstMipIndex = INDEX_NONE;
		int32 LastMipIndex = INDEX_NONE;
	};

	// Pending async requests created in GetMips().
	TArray<TUniquePtr<IAsyncReadRequest>, TInlineAllocator<MAX_TEXTURE_MIP_COUNT>> IORequests;
	// The list of relevant files used for reading texture mips.
	TArray<FFileInfo, TInlineAllocator<2>> FileInfos;

	// Whether async read requests must be created with high priority (executes faster). 
	bool bPrioritizedIORequest = false;
	// Whether async read requests where cancelled for any reasons.
	bool bIORequestCancelled = false;

	// A callback to be executed once all IO pending requests are completed.
	FAsyncFileCallBack AsyncFileCallBack;

	// Helper to configure the AsyncFileCallBack.
	void SetAsyncFileCallback(const FTextureUpdateSyncOptions& SyncOptions);

	// Release / cancel any pending async file requests.
	void ClearIORequests();
};
