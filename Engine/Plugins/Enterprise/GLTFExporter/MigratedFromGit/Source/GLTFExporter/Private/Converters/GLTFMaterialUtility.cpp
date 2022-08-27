// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMaterialUtility.h"

#include "Engine/TextureRenderTarget2D.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Modules/ModuleManager.h"

#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"

UTexture2D* FGLTFMaterialUtility::CreateTransientTexture(const TArray<FColor>& Pixels, const FIntPoint& TextureSize, const EPixelFormat& TextureFormat)
{
	check(TextureSize.X * TextureSize.Y == Pixels.Num());

	UTexture2D* Texture = UTexture2D::CreateTransient(TextureSize.X, TextureSize.Y, TextureFormat);
	void* MipData = Texture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);

	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * Pixels.GetTypeSize());
	Texture->PlatformData->Mips[0].BulkData.Unlock();

	// Texture->SRGB = false;	// TODO: Do we need this?
	Texture->CompressionNone = true;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->CompressionSettings = TC_Default;

	Texture->UpdateResource();
	return Texture;
}

bool FGLTFMaterialUtility::CombineTextures(TArray<FColor>& OutPixels, const TArray<FGLTFTextureCombineSource>& Sources, const FIntPoint& OutputSize, const EPixelFormat OutputPixelFormat)
{
	UTextureRenderTarget2D* RenderTarget2D = NewObject<UTextureRenderTarget2D>();
	check(RenderTarget2D);

	RenderTarget2D->AddToRoot();
	RenderTarget2D->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	RenderTarget2D->InitCustomFormat(OutputSize.X, OutputSize.Y, OutputPixelFormat, false);
	RenderTarget2D->TargetGamma = 0;

	FRenderTarget* RenderTarget = RenderTarget2D->GameThread_GetRenderTargetResource();
	FCanvas* Canvas = new FCanvas(RenderTarget, nullptr, 0, 0, 0, GMaxRHIFeatureLevel);

	Canvas->SetRenderTarget_GameThread(RenderTarget);
	Canvas->Clear(FLinearColor(0, 0, 0, 0));

	const FVector2D TileSize(OutputSize.X, OutputSize.Y);
	const FVector2D TilePosition(0, 0);

	for (const FGLTFTextureCombineSource& Source: Sources)
	{
		check(Source.Texture);

		FCanvasTileItem TileItem(TilePosition, Source.Texture->Resource, TileSize, Source.TintColor);

		TileItem.BlendMode = Source.BlendMode;
		TileItem.Draw(Canvas);
	}

	Canvas->Flush_GameThread();
	FlushRenderingCommands();
	Canvas->SetRenderTarget_GameThread(nullptr);
	FlushRenderingCommands();

	const bool bReadSuccessful = RenderTarget->ReadPixels(OutPixels);

	// Clean up.
	RenderTarget2D->ReleaseResource();
	RenderTarget2D->RemoveFromRoot();
	RenderTarget2D = nullptr;
	delete Canvas;

	return bReadSuccessful;
}

UTexture2D* FGLTFMaterialUtility::BakeMaterialProperty(const FIntPoint OutputSize, const EMaterialProperty& MaterialProperty, const UMaterialInterface* Material)
{
	TArray<FMeshData*> MeshSettings;

	FMeshData* MeshSet = new FMeshData();

	MeshSet->TextureCoordinateBox = FBox2D(FVector2D(0, 0), FVector2D(1, 1));

	// TODO: Do we need to fill in any info in MeshSet?

	MeshSettings.Add(MeshSet);

	TArray<FMaterialData*> MatSettings;

	FMaterialData* MatSet = new FMaterialData();
	MatSet->Material = const_cast<UMaterialInterface*>(Material);
	MatSet->PropertySizes.Add(MaterialProperty, OutputSize);

	MatSettings.Add(MatSet);

	TArray<FBakeOutput> BakeOutputs;
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");

	Module.BakeMaterials(MatSettings, MeshSettings, BakeOutputs);

	const FBakeOutput& Output = BakeOutputs[0];

	// NOTE: The texture-format is mapped per property in the MaterialBaking-module via PerPropertyFormat.
	// It's PF_B8G8R8A8 for all properties except for MP_EmissiveColor (which is PF_FloatRGBA).
	// TODO: Should we handle cases the format *may* be something other that what we expect?
	return CreateTransientTexture(
		Output.PropertyData.FindChecked(MaterialProperty),
		Output.PropertySizes.FindChecked(MaterialProperty),
		PF_B8G8R8A8);
}

FGLTFJsonTextureIndex FGLTFMaterialUtility::AddCombinedTexture(FGLTFConvertBuilder& Builder, const TArray<FGLTFTextureCombineSource>& CombineSources, const FIntPoint TextureSize, const FString& TextureName, const EGLTFJsonTextureFilter Filter, const EGLTFJsonTextureWrap Wrap)
{
	check(CombineSources.Num() > 0);

	TArray<FColor> Pixels;
	const EPixelFormat PixelFormat = CombineSources[0].Texture->GetPixelFormat();

	if (!CombineTextures(Pixels, CombineSources, TextureSize, PixelFormat))
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	// TODO: maybe we should reuse existing samplers?
	FGLTFJsonSampler JsonSampler;
	JsonSampler.Name = TextureName;
	JsonSampler.MagFilter = JsonSampler.MinFilter = Filter;
	JsonSampler.WrapS = JsonSampler.WrapT = Wrap;

	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = TextureName;
	JsonTexture.Sampler = Builder.AddSampler(JsonSampler);
	JsonTexture.Source = Builder.AddImage(Pixels, TextureSize.X, TextureSize.Y, PixelFormat, TextureName);

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
