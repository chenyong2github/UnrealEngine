// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "WidgetBlueprintThumbnailRenderer.generated.h"

class FCanvas;
class FRenderTarget;
class UTextureRenderTarget2D;

UCLASS()
class UMGEDITOR_API UWidgetBlueprintThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()
		
	//~ Begin UThumbnailRenderer Object
	bool CanVisualizeAsset(UObject* Object) override;
	void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;

private:
	TTuple<FVector2D, FVector2D> GetScaledSizeAndOffset(float ImgWidth, float ImgHeight, float ThumbnailWidth, float ThumbnailHeight) const;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget2D;

};
