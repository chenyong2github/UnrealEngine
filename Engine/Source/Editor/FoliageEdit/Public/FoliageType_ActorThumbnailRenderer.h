// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * This thumbnail renderer displays the actor used by this foliage type
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ThumbnailRendering/BlueprintThumbnailRenderer.h"
#include "FoliageType_ActorThumbnailRenderer.generated.h"


class FCanvas;
class FRenderTarget;

UCLASS(CustomConstructor, Config = Editor)
class UFoliageType_ActorThumbnailRenderer : public UBlueprintThumbnailRenderer
{
	GENERATED_UCLASS_BODY()

	UFoliageType_ActorThumbnailRenderer(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{}

	// UThumbnailRenderer implementation
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas) override;
	virtual bool CanVisualizeAsset(UObject* Object) override;
};
