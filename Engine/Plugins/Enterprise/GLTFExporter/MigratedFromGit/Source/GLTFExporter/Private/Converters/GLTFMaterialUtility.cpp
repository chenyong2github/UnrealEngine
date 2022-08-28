// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFTextureUtility.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Modules/ModuleManager.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#include "Materials/MaterialExpressionCustomOutput.h"

FVector4 FGLTFMaterialUtility::GetPropertyDefaultValue(EMaterialProperty Property)
{
	// TODO: replace with GMaterialPropertyAttributesMap lookup (when public API available)

	switch (Property)
	{
		case MP_EmissiveColor:          return FVector4(0,0,0,0);
		case MP_Opacity:                return FVector4(1,0,0,0);
		case MP_OpacityMask:            return FVector4(1,0,0,0);
		case MP_BaseColor:              return FVector4(0,0,0,0);
		case MP_Metallic:               return FVector4(0,0,0,0);
		case MP_Specular:               return FVector4(.5,0,0,0);
		case MP_Roughness:              return FVector4(.5,0,0,0);
		case MP_Anisotropy:             return FVector4(0,0,0,0);
		case MP_Normal:                 return FVector4(0,0,1,0);
		case MP_Tangent:                return FVector4(1,0,0,0);
		case MP_WorldPositionOffset:    return FVector4(0,0,0,0);
		case MP_WorldDisplacement:      return FVector4(0,0,0,0);
		case MP_TessellationMultiplier: return FVector4(1,0,0,0);
		case MP_SubsurfaceColor:        return FVector4(1,1,1,0);
		case MP_CustomData0:            return FVector4(1,0,0,0);
		case MP_CustomData1:            return FVector4(.1,0,0,0);
		case MP_AmbientOcclusion:       return FVector4(1,0,0,0);
		case MP_Refraction:             return FVector4(1,0,0,0);
		case MP_PixelDepthOffset:       return FVector4(0,0,0,0);
		case MP_ShadingModel:           return FVector4(0,0,0,0);
		default:                        break;
	}

	if (Property >= MP_CustomizedUVs0 && Property <= MP_CustomizedUVs7)
	{
		return FVector4(0,0,0,0);
	}

	check(false);
	return FVector4();
}

const FExpressionInput* FGLTFMaterialUtility::GetInputForProperty(const UMaterialInterface* Material, EMaterialProperty Property)
{
	UMaterial* UnderlyingMaterial = const_cast<UMaterial*>(Material->GetMaterial());
	return UnderlyingMaterial->GetExpressionInputForProperty(Property);
}

const UMaterialExpressionCustomOutput* FGLTFMaterialUtility::GetCustomOutputByName(const UMaterialInterface* Material, const FString& Name)
{
	for (const UMaterialExpression* Expression : Material->GetMaterial()->Expressions)
	{
		const UMaterialExpressionCustomOutput* CustomOutput = Cast<UMaterialExpressionCustomOutput>(Expression);
		if (CustomOutput != nullptr && CustomOutput->GetFunctionName() == Name)
		{
			return CustomOutput;
		}
	}

	return nullptr;
}

UTexture2D* FGLTFMaterialUtility::CreateTransientTexture(const FGLTFPropertyBakeOutput& PropertyBakeOutput, bool bUseSRGB)
{
	return FGLTFTextureUtility::CreateTransientTexture(
		PropertyBakeOutput.Pixels.GetData(),
		PropertyBakeOutput.Pixels.Num() * PropertyBakeOutput.Pixels.GetTypeSize(),
		PropertyBakeOutput.Size,
		PropertyBakeOutput.PixelFormat,
		bUseSRGB);
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

FGLTFPropertyBakeOutput FGLTFMaterialUtility::BakeMaterialProperty(const FIntPoint& OutputSize, EMaterialProperty Property, const UMaterialInterface* Material, bool bCopyAlphaFromRedChannel)
{
	if (Property == MP_CustomData0 || Property == MP_CustomData1)
	{
		// TODO: add special support
	}

	TArray<FMeshData*> MeshSettings;

	FMeshData MeshSet;
	MeshSet.TextureCoordinateBox = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };

	// TODO: Do we need to fill in any more info in MeshSet?

	MeshSettings.Add(&MeshSet);

	TArray<FMaterialData*> MatSettings;

	FMaterialData MatSet;
	MatSet.Material = const_cast<UMaterialInterface*>(Material);
	MatSet.PropertySizes.Add(Property, OutputSize);

	MatSettings.Add(&MatSet);

	TArray<FBakeOutput> BakeOutputs;
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");

	Module.BakeMaterials(MatSettings, MeshSettings, BakeOutputs);

	FBakeOutput BakeOutput = BakeOutputs[0];

	TArray<FColor> BakedPixels = MoveTemp(BakeOutput.PropertyData.FindChecked(Property));
	const FIntPoint BakedSize = BakeOutput.PropertySizes.FindChecked(Property);
	const float EmissiveScale = BakeOutput.EmissiveScale;

	if (bCopyAlphaFromRedChannel)
	{
		for (FColor& Pixel: BakedPixels)
		{
			Pixel.A = Pixel.R;
		}
	}
	else
	{
		// NOTE: alpha is 0 by default after baking a property, but we prefer 255 (1.0).
		// It makes it easier to view the exported textures.
		for (FColor& Pixel: BakedPixels)
		{
			Pixel.A = 255;
		}
	}

	FGLTFPropertyBakeOutput PropertyBakeOutput(Property, PF_B8G8R8A8, BakedPixels, BakedSize, EmissiveScale);

	if (BakedPixels.Num() == 1)
	{
		const FColor& Pixel = BakedPixels[0];

		PropertyBakeOutput.bIsConstant = true;

		// TODO: is the current conversion from sRGB => linear correct?
		// It seems to give correct results for some properties, but not all.
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
	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), TextureSize, TextureName);

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
