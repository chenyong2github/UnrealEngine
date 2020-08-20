// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "TextureShareEnums.generated.h"

/** Synchronize Session state events (BeginSession/EndSession) */
UENUM(BlueprintType, Category = "TextureShare")
enum class ETextureShareBPSyncConnect :  uint8
{
	/** [Default] - Use module global settings */
	Default = 0,
	/** [None] - do not wait for remote process */
	None,
	/** [SyncSession] - waiting until remote process not inside BeginSession()/EndSession() */
	SyncSession
};

/** Synchronize frame events (BeginFrame/EndFrame) */
UENUM(BlueprintType, Category = "TextureShare")
enum class ETextureShareBPSyncFrame : uint8
{
	/** [Default] - Use module global settings */
	Default = 0,
	/** [None] - Unordered frames */
	None,
	/** [FrameSync] - waiting until remote process frame index is equal */
	FrameSync
};

/** Synchronize texture events (LockTexture/UnlockTexture) */
UENUM(BlueprintType, Category = "TextureShare")
enum class ETextureShareBPSyncSurface : uint8
{
	/** [Default] - Use module global settings */
	Default = 0,
	/** [None] - Skip unpaired texture. Unordered read\write operations */
	None,
	/** [SyncRead] - Skip unpaired texture. Waiting until other process changed texture (readOP is wait for writeOP from remote process completed) */
	SyncRead,
	/** [SyncPairingRead] - Required texture pairing. Waiting until other process changed texture (readOP is wait for writeOP from remote process completed) */
	SyncPairingRead
};
