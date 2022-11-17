// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/GLTFDelayedTextureTasks.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Utilities/GLTFCoreUtilities.h"
#include "Converters/GLTFTextureUtility.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"

FString FGLTFDelayedTexture2DTask::GetName()
{
	return Texture2D->GetName();
}

void FGLTFDelayedTexture2DTask::Process()
{
	FGLTFTextureUtility::FullyLoad(Texture2D);
	Texture2D->GetName(JsonTexture->Name);

	const bool bFromSRGB = Texture2D->SRGB;
	if (bFromSRGB != bToSRGB)
	{
		JsonTexture->Name += bToSRGB ? TEXT("_sRGB") : TEXT("_Linear");
	}

	const FIntPoint Size = FGLTFTextureUtility::GetInGameSize(Texture2D);
	UTextureRenderTarget2D* RenderTarget = FGLTFTextureUtility::CreateRenderTarget(Size, false);

	// TODO: preserve maximum image quality (avoid compression artifacts) by copying source data (and adjustments) to a temp texture
	FGLTFTextureUtility::DrawTexture(RenderTarget, Texture2D);

	TGLTFSharedArray<FColor> Pixels;
	if (!FGLTFTextureUtility::ReadPixels(RenderTarget, *Pixels))
	{
		Builder.LogWarning(FString::Printf(TEXT("Failed to read pixels for 2D texture %s"), *JsonTexture->Name));
		return;
	}

	if (Builder.ExportOptions->bAdjustNormalmaps && Texture2D->IsNormalMap())
	{
		// TODO: add support for adjusting normals in GLTFNormalMapPreview instead
		FGLTFTextureUtility::FlipGreenChannel(*Pixels);
	}

	FGLTFTextureUtility::TransformColorSpace(*Pixels, bFromSRGB, bToSRGB);

	const bool bIgnoreAlpha = FGLTFTextureUtility::IsAlphaless(Texture2D->GetPixelFormat());
	const EGLTFTextureType Type = Texture2D->IsNormalMap() ? EGLTFTextureType::Normalmaps : EGLTFTextureType::None;

	JsonTexture->Source = Builder.AddUniqueImage(Pixels, Size, bIgnoreAlpha, Type, JsonTexture->Name);
	JsonTexture->Sampler = Builder.AddUniqueSampler(Texture2D);
}

FString FGLTFDelayedTextureRenderTarget2DTask::GetName()
{
	return RenderTarget2D->GetName();
}

void FGLTFDelayedTextureRenderTarget2DTask::Process()
{
	FGLTFTextureUtility::FullyLoad(RenderTarget2D);
	RenderTarget2D->GetName(JsonTexture->Name);

	const bool bFromSRGB = RenderTarget2D->SRGB;
	if (bFromSRGB != bToSRGB)
	{
		JsonTexture->Name += bToSRGB ? TEXT("_sRGB") : TEXT("_Linear");
	}

	const bool bIsHDR = FGLTFTextureUtility::IsHDR(RenderTarget2D);
	const FIntPoint Size = { RenderTarget2D->SizeX, RenderTarget2D->SizeY };

	TGLTFSharedArray<FColor> Pixels;
	if (!FGLTFTextureUtility::ReadPixels(RenderTarget2D, *Pixels))
	{
		Builder.LogWarning(FString::Printf(TEXT("Failed to read pixels for 2D render target %s"), *JsonTexture->Name));
		return;
	}

	FGLTFTextureUtility::TransformColorSpace(*Pixels, bFromSRGB, bToSRGB);

	const bool bIgnoreAlpha = FGLTFTextureUtility::IsAlphaless(RenderTarget2D->GetFormat());
	const EGLTFTextureType Type = bIsHDR ? EGLTFTextureType::HDR : EGLTFTextureType::None;

	JsonTexture->Source = Builder.AddUniqueImage(Pixels, Size, bIgnoreAlpha, Type, JsonTexture->Name);
	JsonTexture->Sampler = Builder.AddUniqueSampler(RenderTarget2D);
}
