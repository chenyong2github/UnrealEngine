// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Data/PCGTextureData.h"
#include "Engine/TextureRenderTarget2D.h"

#include "PCGRenderTargetData.generated.h"

//TODO: It's possible that caching the result in this class is not as efficient as it could be
// if we expect to sample in different ways (e.g. channel) in the same render target
UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGRenderTargetData : public UPCGBaseTextureData
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = RenderTarget)
	void Initialize(UTextureRenderTarget2D* InRenderTarget, const FTransform& InTransform);

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Properties)
	UTextureRenderTarget2D* RenderTarget = nullptr;
};