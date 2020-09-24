// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "TextureShareCoreContainers.h"
#include "SharedResourceEnums.h"

#include "Containers/TextureShareCoreEnums.h"
#include "Containers/TextureShareCoreGenericContainers.h"

// Server create&adopt textures formats for client request
struct FSharedResourceTexture
{
	struct FSharingData
	{
		// GPU-GPU share purpose (DX11,DX12)
		void* SharedHandle = nullptr;

		// MGPU node index, undefined
		int MGPUIndex = -1;

		// Unique handle name
		FGuid SharedHandleGuid;

		bool IsValid()
		{
			return SharedHandle != nullptr;
		}
	};

	bool IsUsed() const
	{
		switch (State)
		{
		case ESharedResourceTextureState::Undefined:
		case ESharedResourceTextureState::Disabled:
			return false;
		default:
			break;
		}
		return true;
	}
	
	// remote side copy handle after connected
	inline bool IsSharedHandleConnected(const FSharedResourceTexture& RemoteTexture) const
	{
		return SharingData.SharedHandle == RemoteTexture.SharingData.SharedHandle;
	}

	void ReleaseConnection()
	{
		SharingData.SharedHandle = nullptr;

		// Reset texture state
		switch (State)
		{
		case ESharedResourceTextureState::Undefined:
		case ESharedResourceTextureState::INVALID:
			break;
		default:
			State = ESharedResourceTextureState::Ready;
			break;
		}
	}

	FTextureShareSurfaceDesc    TextureDesc;
	FSharingData                SharingData;

	int                         Index;
	TCHAR                       Name[MaxTextureShareItemNameLength] = { 0 }; /** Custom Name of texture resource, used to client-server logic connecting */

	ESharedResourceTextureState  State = ESharedResourceTextureState::Undefined;

	/** Texture operation type */
	ETextureShareSurfaceOp       OperationType;

	// Read\Write ops sync
	uint64 AccessSyncFrame = 0;
};

struct FSharedResourceProcessData
{
	ETextureShareDevice         DeviceType = ETextureShareDevice::Undefined;
	ETextureShareSource         Source = ETextureShareSource::Undefined;

	FTextureShareSyncPolicy     SyncMode;
	uint64                      SyncFrame = 0;
	ETextureShareFrameState     FrameState = ETextureShareFrameState::None;

	FSharedResourceTexture      Textures[MaxTextureShareItemTexturesCount];

	// Default MGPU node index, Use GPU0
	uint32 DefaultMGPUIndex = 0;

	FORCEINLINE uint32 GetTextureMGPUIndex(int TextureIndex)
	{
		int TextureMGPUIndex = Textures[TextureIndex].SharingData.MGPUIndex;
		return (TextureMGPUIndex < 0) ? DefaultMGPUIndex : (uint32)TextureMGPUIndex;
	}

	FTextureShareAdditionalData AdditionalData;

	FORCEINLINE uint64 GetTextureAccessSyncFrame() const
	{
		return SyncFrame + 1; // Use this value for texture AccessSyncFrame after  unlock_texture
	}

	FORCEINLINE bool IsValid() const
	{
		return DeviceType != ETextureShareDevice::Undefined;
	}

	FORCEINLINE void DiscardData()
	{
		DeviceType = ETextureShareDevice::Undefined;
	}

	FORCEINLINE void ResetSyncFrame()
	{
		SyncFrame = 0;
		for (int i = 0; i < MaxTextureShareItemTexturesCount; i++)
		{
			Textures[i].AccessSyncFrame = 0;
		}
	}

	FORCEINLINE bool IsFrameLockedNow() const
	{
		return FrameState != ETextureShareFrameState::None;
	}

	FORCEINLINE bool IsFrameOpLockedNow() const
	{
		return FrameState == ETextureShareFrameState::LockedOp;
	}
};

struct FSharedResourceSessionData
{
	FSharedResourceProcessData ServerData;
	FSharedResourceProcessData ClientData;

	// sync order is [server = client+1]
	// Server(Lock +1) ... Client(Lock+1) .. Unlock
	bool IsSyncFrameValid(bool bIsClient) const
	{
		if (bIsClient)
		{
			// Client wait for server go inside frames lock, server frame is +1
			return ServerData.SyncFrame == (ClientData.SyncFrame + 1);
		}
		else
		{
			if (ClientData.IsFrameLockedNow())
			{
				// Wait while client unlock current frame before begin new
				return false;
			}

			// Server wait for client lock for processed frame, frames must be equal
			return ServerData.SyncFrame == ClientData.SyncFrame;
		}
	}

	bool IsFrameSyncLost() const
	{
		if (ServerData.SyncFrame < ClientData.SyncFrame)
		{
			// Client overrun
			return true;
		}

		if ((ServerData.SyncFrame - ClientData.SyncFrame) > 2)
		{
			// Server overrun
			return true;
		}

		return false;
	}
};

struct FSharedResourceSessionHeader
{
	ESharedResourceProcessState   ProcessState[(uint8)ETextureShareProcess::COUNT];
	TCHAR                         SessionName[MaxTextureShareItemSessionName] = { 0 };

	FSharedResourceSessionHeader()
	{
		for (int i = 0; i < (uint8)ETextureShareProcess::COUNT; i++)
		{
			ProcessState[i] = ESharedResourceProcessState::Undefined;
		}
	}

	FORCEINLINE bool IsFreeSession() const
	{
		return (SessionName[0] == 0) || (ProcessState[(uint8)ETextureShareProcess::Server] == ESharedResourceProcessState::Undefined && ProcessState[(uint8)ETextureShareProcess::Client] == ESharedResourceProcessState::Undefined);
	}

	FORCEINLINE bool IsProcessUsed(ETextureShareProcess ProcessType) const
	{
		return (SessionName[0] != 0) && ProcessState[(uint8)ProcessType] == ESharedResourceProcessState::Used;
	}

};

struct FSharedResourcePublicData
{
	FSharedResourceSessionHeader SessionHeader[MaxTextureShareItemSessionsCount];
	FSharedResourceSessionData   SessionData[MaxTextureShareItemSessionsCount];
};
