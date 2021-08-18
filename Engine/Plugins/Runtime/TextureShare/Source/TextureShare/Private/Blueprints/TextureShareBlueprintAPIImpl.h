// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "InputCoreTypes.h"
#include "Blueprints/ITextureShareBlueprintAPI.h"

#include "TextureShareBlueprintAPIImpl.generated.h"

/**
 * Blueprint API interface implementation
 */
UCLASS()
class UTextureShareAPIImpl
	: public UObject
	, public ITextureShareBlueprintAPI
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Create TextureShare"), Category = "TextureShare")
	virtual bool CreateTextureShare(const FString ShareName, FTextureShareBPSyncPolicy SyncMode, bool bIsServer = true, float SyncWaitTime = 0.03f) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Delete TextureShare"), Category = "TextureShare")
	virtual bool ReleaseTextureShare(const FString ShareName) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Link SceneContext To TextureShare"), Category = "TextureShare")
	virtual bool LinkSceneContextToShare(const FString ShareName, int StereoscopicPass = 0, bool bIsEnabled = true) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Apply TextureShare Postprocess"), Category = "TextureShare")
	virtual bool ApplyTextureSharePostprocess(const FString ShareName, const FTextureShareBPPostprocess& Postprocess) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get local process SyncPolicy Settings"), Category = "TextureShare")
	virtual FTextureShareBPSyncPolicySettings GetSyncPolicySettings() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set local process SyncPolicy Settings"), Category = "TextureShare")
	virtual void SetSyncPolicySettings(const FTextureShareBPSyncPolicySettings& InSyncPolicySettings) override;
};

