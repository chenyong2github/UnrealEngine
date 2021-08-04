// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"

#include "DisplayClusterConfigurationTypes_TextureShare.generated.h"

/** Synchronize Session state events (BeginSession/EndSession) */
UENUM(BlueprintType, Category = "TextureShare")
enum class ETextureShareSyncConnectDisplayCluster : uint8
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
enum class ETextureShareSyncFrameDisplayCluster : uint8
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
enum class ETextureShareSyncSurfaceDisplayCluster : uint8
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


USTRUCT(BlueprintType)
struct FTextureShareSyncPolicyDisplayCluster
{
	GENERATED_BODY()

	// Synchronize Session state events (BeginSession/EndSession)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	ETextureShareSyncConnectDisplayCluster Connection = ETextureShareSyncConnectDisplayCluster::Default;

	// Synchronize frame events (BeginFrame/EndFrame)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	ETextureShareSyncFrameDisplayCluster   Frame = ETextureShareSyncFrameDisplayCluster::Default;

	// Synchronize texture events (LockTexture/UnlockTexture)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare")
	ETextureShareSyncSurfaceDisplayCluster Texture = ETextureShareSyncSurfaceDisplayCluster::Default;
};

USTRUCT(Blueprintable)
struct FDisplayClusterConfigurationTextureShare_Viewport
{
	GENERATED_BODY()

	// Allow this viewport to be shared through the TextureShare
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare", meta = (DisplayName = "Share Viewport"))
	bool bIsEnabled = false;

	// TextureShare synchronization settings, custom gui added in the configurator for Linux compatibility
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextureShare", meta = (DisplayName = "Sync Settings"))
	FTextureShareSyncPolicyDisplayCluster SyncSettings;
};
