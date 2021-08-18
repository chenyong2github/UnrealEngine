// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TextureResource.h"

#include "TextureShareContainers.h"

#include "ITextureShareBlueprintAPI.generated.h"

class UMaterialInstanceDynamic;

UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class UTextureShareBlueprintAPI : public UInterface
{
	GENERATED_BODY()
};

class ITextureShareBlueprintAPI
{
	GENERATED_BODY()

public:

	/**
	 * Create new textureshare object
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 * @param SyncMode  - Sync options
	 * @param bIsSlave  - process type for pairing (master or slave)
	 *
	 * @return True if the success
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Create TextureShare"), Category = "TextureShare")
	virtual bool CreateTextureShare(const FString ShareName, FTextureShareBPSyncPolicy SyncMode, bool bIsServer = true, float SyncWaitTime = 0.03f) = 0;

	/**
	 * Release exist textureshare object
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 *
	 * @return True if the success
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Delete TextureShare"), Category = "TextureShare")
	virtual bool ReleaseTextureShare(const FString ShareName) = 0;

	/**
	 * Link\Unlink SceneContext capture (for specified StereoscopicPass) to exist textureshare object
	 *
	 * @param ShareName        - Unique share name (case insensitive)
	 * @param StereoscopicPass - stereo rendering and nDisplay purpose
	 * @param bIsEnabled       - true to create link
	 *
	 * @return True if the success
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Link SceneContext To TextureShare"), Category = "TextureShare")
	virtual bool LinkSceneContextToShare(const FString ShareName, int StereoscopicPass = 0, bool bIsEnabled = true) = 0;

	/**
	 * Send from Input[], wait and receive result to Output[] from the remote process
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 * @param Postprocess - Textures to exchange
	 *
	 * @return True if the success
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Apply TextureShare Postprocess"), Category = "TextureShare")
	virtual bool ApplyTextureSharePostprocess(const FString ShareName, const FTextureShareBPPostprocess& Postprocess) = 0;

	/**
	 * Get global synchronization settings
	 * @return settings data struct
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get local process SyncPolicy Settings"), Category = "TextureShare")
	virtual FTextureShareBPSyncPolicySettings GetSyncPolicySettings() const = 0;

	/**
	 * Get global synchronization settings
	 *
	 * @param Process              - Process logic type: server or client
	 * @param InSyncPolicySettings - settings data struct
	 */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set local process SyncPolicy Settings"), Category = "TextureShare")
	virtual void SetSyncPolicySettings(const FTextureShareBPSyncPolicySettings& InSyncPolicySettings) = 0;
};
