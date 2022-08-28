// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMaterialUtility.h"

#include "Engine/TextureRenderTarget2D.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Modules/ModuleManager.h"

#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"

UTexture2D* FGLTFMaterialUtility::CreateTransientTexture(const FGLTFPropertyBakeOutput& PropertyBakeOutput, bool bUseSRGB)
{
	return CreateTransientTexture(
		PropertyBakeOutput.Pixels,
		PropertyBakeOutput.Size,
		PropertyBakeOutput.PixelFormat,
		bUseSRGB);
}

UTexture2D* FGLTFMaterialUtility::CreateTransientTexture(const TArray<FColor>& Pixels, const FIntPoint& TextureSize, EPixelFormat TextureFormat, bool bUseSRGB)
{
	check(TextureSize.X * TextureSize.Y == Pixels.Num());

	UTexture2D* Texture = UTexture2D::CreateTransient(TextureSize.X, TextureSize.Y, TextureFormat);
	void* MipData = Texture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);

	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize());
	Texture->PlatformData->Mips[0].BulkData.Unlock();

	Texture->SRGB = bUseSRGB ? 1 : 0;
	Texture->CompressionNone = true;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->CompressionSettings = TC_Default;

	Texture->UpdateResource();
	return Texture;
}

bool FGLTFMaterialUtility::CombineTextures(TArray<FColor>& OutPixels, const TArray<FGLTFTextureCombineSource>& Sources, const FIntPoint& OutputSize, const EPixelFormat OutputPixelFormat)
{
	UTextureRenderTarget2D* RenderTarget2D = NewObject<UTextureRenderTarget2D>();

	RenderTarget2D->AddToRoot();
	RenderTarget2D->ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	RenderTarget2D->InitCustomFormat(OutputSize.X, OutputSize.Y, OutputPixelFormat, false);
	RenderTarget2D->TargetGamma = 0.0f;

	FRenderTarget* RenderTarget = RenderTarget2D->GameThread_GetRenderTargetResource();
	FCanvas Canvas(RenderTarget, nullptr, 0.0f, 0.0f, 0.0f, GMaxRHIFeatureLevel);

	Canvas.SetRenderTarget_GameThread(RenderTarget);
	Canvas.Clear({ 0.0f, 0.0f, 0.0f, 0.0f });

	const FVector2D TileSize(OutputSize.X, OutputSize.Y);
	const FVector2D TilePosition(0.0f, 0.0f);

	for (const FGLTFTextureCombineSource& Source: Sources)
	{
		check(Source.Texture);

		FCanvasTileItem TileItem(TilePosition, Source.Texture->Resource, TileSize, Source.TintColor);

		TileItem.BlendMode = Source.BlendMode;
		TileItem.Draw(&Canvas);
	}

	Canvas.Flush_GameThread();
	FlushRenderingCommands();
	Canvas.SetRenderTarget_GameThread(nullptr);
	FlushRenderingCommands();

	const bool bReadSuccessful = RenderTarget->ReadPixels(OutPixels);

	// Clean up.
	RenderTarget2D->ReleaseResource();
	RenderTarget2D->RemoveFromRoot();
	RenderTarget2D = nullptr;

	return bReadSuccessful;
}

FGLTFPropertyBakeOutput FGLTFMaterialUtility::BakeMaterialProperty(const FIntPoint& OutputSize, EMaterialProperty MaterialProperty, const UMaterialInterface* Material, bool bCopyAlphaFromRedChannel)
{
	TArray<FMeshData*> MeshSettings;

	FMeshData MeshSet;
	MeshSet.TextureCoordinateBox = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };

	// TODO: Do we need to fill in any more info in MeshSet?

	MeshSettings.Add(&MeshSet);

	TArray<FMaterialData*> MatSettings;

	FMaterialData MatSet;
	MatSet.Material = const_cast<UMaterialInterface*>(Material);
	MatSet.PropertySizes.Add(MaterialProperty, OutputSize);

	MatSettings.Add(&MatSet);

	TArray<FBakeOutput> BakeOutputs;
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");

	Module.BakeMaterials(MatSettings, MeshSettings, BakeOutputs);

	FBakeOutput BakeOutput = BakeOutputs[0];

	TArray<FColor> BakedPixels = MoveTemp(BakeOutput.PropertyData.FindChecked(MaterialProperty));
	const FIntPoint BakedSize = BakeOutput.PropertySizes.FindChecked(MaterialProperty);
	const float EmissiveScale = BakeOutput.EmissiveScale;

	if (bCopyAlphaFromRedChannel)
	{
		// NOTE: Baked property-textures only have values in RGB, not A.
		// If someone wants to use the alpha-channel of the baked texture (for compositing),
		// it needs to be filled with values first.
		// Since scalar properties are baked to R + G + B, it doesn't matter which channel
		// we use as source. For now it's set to R.
		// TODO: Determine if we can get the baking module to somehow output values in A automatically.
		// If not, see if the copy-operation can be performed via the canvas (using a proxy-material or similar).
		for (FColor& Pixel: BakedPixels)
		{
			Pixel.A = Pixel.R;
		}
	}

	FGLTFPropertyBakeOutput PropertyBakeOutput(MaterialProperty, PF_B8G8R8A8, BakedPixels, BakedSize, EmissiveScale);

	if (BakedPixels.Num() == 1)
	{
		const FColor& Pixel = BakedPixels[0];

		PropertyBakeOutput.bIsConstant = true;

		// TODO: is the current conversion from sRGB => linear correct?
		PropertyBakeOutput.ConstantValue = Pixel;
	}

	return PropertyBakeOutput;
}

FGLTFJsonTextureIndex FGLTFMaterialUtility::AddCombinedTexture(FGLTFConvertBuilder& Builder, const TArray<FGLTFTextureCombineSource>& CombineSources, const FIntPoint& TextureSize, const FString& TextureName, EGLTFJsonTextureFilter MinFilter, EGLTFJsonTextureFilter MagFilter, EGLTFJsonTextureWrap WrapS, EGLTFJsonTextureWrap WrapT)
{
	check(CombineSources.Num() > 0);

	TArray<FColor> Pixels;
	const EPixelFormat PixelFormat = CombineSources[0].Texture->GetPixelFormat();

	if (!CombineTextures(Pixels, CombineSources, TextureSize, PixelFormat))
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return AddTexture(Builder, Pixels, TextureSize, TextureName, PixelFormat, MinFilter, MagFilter, WrapS, WrapT);
}

FGLTFJsonTextureIndex FGLTFMaterialUtility::AddTexture(FGLTFConvertBuilder& Builder, const TArray<FColor>& Pixels, const FIntPoint& TextureSize, const FString& TextureName, EPixelFormat PixelFormat, EGLTFJsonTextureFilter MinFilter, EGLTFJsonTextureFilter MagFilter, EGLTFJsonTextureWrap WrapS, EGLTFJsonTextureWrap WrapT)
{
	// TODO: maybe we should reuse existing samplers?
	FGLTFJsonSampler JsonSampler;
	JsonSampler.Name = TextureName;
	JsonSampler.MinFilter = MinFilter;
	JsonSampler.MagFilter = MagFilter;
	JsonSampler.WrapS = WrapS;
	JsonSampler.WrapT = WrapT;

	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = TextureName;
	JsonTexture.Sampler = Builder.AddSampler(JsonSampler);
	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), Pixels.Num() * sizeof(FColor), TextureSize.X, TextureSize.Y, PixelFormat, TextureName);

	return Builder.AddTexture(JsonTexture);
}

FLinearColor FGLTFMaterialUtility::GetMask(const FExpressionInput& ExpressionInput)
{
	return FLinearColor(ExpressionInput.MaskR, ExpressionInput.MaskG, ExpressionInput.MaskB, ExpressionInput.MaskA);
}

uint32 FGLTFMaterialUtility::GetMaskComponentCount(const FExpressionInput& ExpressionInput)
{
	return ExpressionInput.MaskR + ExpressionInput.MaskG + ExpressionInput.MaskB + ExpressionInput.MaskA;
}
