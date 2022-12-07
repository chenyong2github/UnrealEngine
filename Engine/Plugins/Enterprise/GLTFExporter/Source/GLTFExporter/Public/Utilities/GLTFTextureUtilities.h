// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Engine/TextureRenderTarget2D.h"
#endif

class FTexture;
class UTexture;
class UTextureRenderTarget2D;

class GLTFEXPORTER_API FGLTFTextureUtilities
{
public:

	static UTextureRenderTarget2D* CombineBaseColorAndOpacity(const UTexture* BaseColorTexture, const UTexture* OpacityTexture);
	static UTextureRenderTarget2D* CombineMetallicAndRoughness(const UTexture* MetallicTexture, const UTexture* RoughnessTexture);

	static void CombineBaseColorAndOpacity(const UTexture* BaseColorTexture, const UTexture* OpacityTexture, UTextureRenderTarget2D* OutputRenderTarget);
	static void CombineMetallicAndRoughness(const UTexture* MetallicTexture, const UTexture* RoughnessTexture, UTextureRenderTarget2D* OutputRenderTarget);

	static void CombineTextures(const UTexture* TextureA, const FMatrix& ColorTransformA, const UTexture* TextureB, const FMatrix& ColorTransformB, const FLinearColor& BackgroundColor, UTextureRenderTarget2D* OutRenderTarget);

	static UTextureRenderTarget2D* CreateRenderTarget(const FIntPoint& Size, const TCHAR* Name);

	static FIntPoint GetMaxSize(const UTexture* TextureA, const UTexture* TextureB);

private:

	static FTexture* GetWhiteTexture();
};
