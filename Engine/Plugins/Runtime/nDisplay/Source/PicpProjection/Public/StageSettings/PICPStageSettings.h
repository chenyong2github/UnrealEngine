// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PICPStageSettings.generated.h"

class UTextureRenderTarget2D;


UCLASS()
class APICPStageSettings : public AActor
{
	GENERATED_BODY()

public:
	APICPStageSettings();

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Cached Render Target"), Category = "nDisplay PICP Projection")
	UTextureRenderTarget2D* GetRenderTargetCached(const FString& RenderTextureId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Cached Render Target"), Category = "nDisplay PICP Projection")
	void SetRenderTargetCached(const FString& RenderTextureId, UTextureRenderTarget2D* RenderTexture);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Clear RenderTarget Cache"), Category = "nDisplay PICP Projection")
	void ClearRenderTargetCache();

private:
	mutable FCriticalSection InternalsSyncScope;
	static TMap<FString, UTextureRenderTarget2D*> RenderTargetCache;
};
