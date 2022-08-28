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
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"

const TCHAR* FGLTFMaterialUtility::GetPropertyName(EMaterialProperty Property)
{
	// TODO: replace this hardcoded list with some lookup function that should be in the engine.

	switch (Property)
	{
		case MP_EmissiveColor:          return TEXT("EmissiveColor");
		case MP_Opacity:                return TEXT("Opacity");
		case MP_OpacityMask:            return TEXT("OpacityMask");
		case MP_BaseColor:              return TEXT("BaseColor");
		case MP_Metallic:               return TEXT("Metallic");
		case MP_Specular:               return TEXT("Specular");
		case MP_Roughness:              return TEXT("Roughness");
		case MP_Anisotropy:             return TEXT("Anisotropy");
		case MP_Normal:                 return TEXT("Normal");
		case MP_Tangent:                return TEXT("Tangent");
		case MP_WorldPositionOffset:    return TEXT("WorldPositionOffset");
		case MP_WorldDisplacement:      return TEXT("WorldDisplacement");
		case MP_TessellationMultiplier: return TEXT("TessellationMultiplier");
		case MP_SubsurfaceColor:        return TEXT("SubsurfaceColor");
		case MP_CustomData0:            return TEXT("ClearCoat");
		case MP_CustomData1:            return TEXT("ClearCoatRoughness");
		case MP_AmbientOcclusion:       return TEXT("AmbientOcclusion");
		case MP_Refraction:             return TEXT("Refraction");
		case MP_CustomizedUVs0:         return TEXT("CustomizedUV0");
		case MP_CustomizedUVs1:         return TEXT("CustomizedUV1");
		case MP_CustomizedUVs2:         return TEXT("CustomizedUV2");
		case MP_CustomizedUVs3:         return TEXT("CustomizedUV3");
		case MP_CustomizedUVs4:         return TEXT("CustomizedUV4");
		case MP_CustomizedUVs5:         return TEXT("CustomizedUV5");
		case MP_CustomizedUVs6:         return TEXT("CustomizedUV6");
		case MP_CustomizedUVs7:         return TEXT("CustomizedUV7");
		case MP_PixelDepthOffset:       return TEXT("PixelDepthOffset");
		case MP_ShadingModel:           return TEXT("ShadingModel");
		case MP_MaterialAttributes:     return TEXT("MaterialAttributes");
		case MP_CustomOutput:           return TEXT("ClearCoatBottomNormal");
		default:                        checkNoEntry();
	}

	return TEXT("");
}

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

	// TODO: replace workaround for ClearCoatBottomNormal
	if (Property == MP_CustomOutput)
	{
		return FVector4(0,0,1,0);
	}

	check(false);
	return FVector4();
}

FVector4 FGLTFMaterialUtility::GetPropertyMask(EMaterialProperty Property)
{
	// TODO: replace with GMaterialPropertyAttributesMap lookup (when public API available)

	switch (Property)
	{
		case MP_EmissiveColor:          return FVector4(1,1,1,0);
		case MP_Opacity:                return FVector4(1,0,0,0);
		case MP_OpacityMask:            return FVector4(1,0,0,0);
		case MP_BaseColor:              return FVector4(1,1,1,0);
		case MP_Metallic:               return FVector4(1,0,0,0);
		case MP_Specular:               return FVector4(1,0,0,0);
		case MP_Roughness:              return FVector4(1,0,0,0);
		case MP_Anisotropy:             return FVector4(1,0,0,0);
		case MP_Normal:                 return FVector4(1,1,1,0);
		case MP_Tangent:                return FVector4(1,1,1,0);
		case MP_WorldPositionOffset:    return FVector4(1,1,1,0);
		case MP_WorldDisplacement:      return FVector4(1,1,1,0);
		case MP_TessellationMultiplier: return FVector4(1,0,0,0);
		case MP_SubsurfaceColor:        return FVector4(1,1,1,0);
		case MP_CustomData0:            return FVector4(1,0,0,0);
		case MP_CustomData1:            return FVector4(1,0,0,0);
		case MP_AmbientOcclusion:       return FVector4(1,0,0,0);
		case MP_Refraction:             return FVector4(1,1,0,0);
		case MP_PixelDepthOffset:       return FVector4(1,0,0,0);
		case MP_ShadingModel:           return FVector4(1,0,0,0);
		default:                        break;
	}

	if (Property >= MP_CustomizedUVs0 && Property <= MP_CustomizedUVs7)
	{
		return FVector4(1,1,0,0);
	}

	// TODO: replace workaround for ClearCoatBottomNormal
	if (Property == MP_CustomOutput)
	{
		return FVector4(1,1,1,0);
	}

	check(false);
	return FVector4();
}

const FExpressionInput* FGLTFMaterialUtility::GetInputForProperty(const UMaterialInterface* Material, EMaterialProperty Property)
{
	// TODO: replace workaround for ClearCoatBottomNormal
	if (Property == MP_CustomOutput)
	{
		const UMaterialExpressionCustomOutput* CustomOutput = GetCustomOutputByName(Material, TEXT("ClearCoatBottomNormal"));
		return CustomOutput != nullptr ? &CastChecked<UMaterialExpressionClearCoatNormalCustomOutput>(CustomOutput)->Input : nullptr;
	}

	UMaterial* UnderlyingMaterial = const_cast<UMaterial*>(Material->GetMaterial());
	return UnderlyingMaterial->GetExpressionInputForProperty(Property);
}

const UMaterialExpressionCustomOutput* FGLTFMaterialUtility::GetCustomOutputByName(const UMaterialInterface* Material, const FString& Name)
{
	// TODO: should we also search inside material functions and attribute layers?

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

FGLTFPropertyBakeOutput FGLTFMaterialUtility::BakeMaterialProperty(const FIntPoint& OutputSize, EMaterialProperty Property, const UMaterialInterface* Material, int32 TexCoord, bool bCopyAlphaFromRedChannel)
{
	EMaterialProperty HackProperty = MP_MAX;
	TTuple<int32, UMaterialExpression*> HackPrevState;

	// TODO: replace this hack by adding proper support for ClearCoat properties in MaterialBaking module
	if (Property == MP_CustomData0 || Property == MP_CustomData1 || Property == MP_CustomOutput)
	{
		switch (Property)
		{
			case MP_CustomData0: HackProperty = MP_Metallic; break;
			case MP_CustomData1: HackProperty = MP_Roughness; break;
			case MP_CustomOutput: HackProperty = MP_Normal; break;
			default: checkNoEntry();
		}

		UMaterial* ParentMaterial = const_cast<UMaterial*>(Material->GetMaterial());
		ParentMaterial->Modify();
		{
			const FExpressionInput& Input = *GetInputForProperty(Material, Property);
			FExpressionInput& HackInput = *const_cast<FExpressionInput*>(GetInputForProperty(Material, HackProperty));
			HackPrevState = MakeTuple(HackInput.OutputIndex, HackInput.Expression);
			HackInput.Connect(Input.OutputIndex, Input.Expression);
		}
		ParentMaterial->PostEditChange();

		Property = HackProperty;
	}

	TArray<FMeshData*> MeshSettings;

	FMeshData MeshSet;
	MeshSet.TextureCoordinateBox = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
	MeshSet.TextureCoordinateIndex = TexCoord;

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

	FBakeOutput& BakeOutput = BakeOutputs[0];

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

	// TODO: replace this hack by adding proper support for ClearCoat properties in MaterialBaking module
	if (HackProperty != MP_MAX)
	{
		UMaterial* ParentMaterial = const_cast<UMaterial*>(Material->GetMaterial());
		ParentMaterial->Modify();
		{
			FExpressionInput& HackInput = *const_cast<FExpressionInput*>(GetInputForProperty(Material, HackProperty));
			HackInput.Connect(HackPrevState.Get<0>(), HackPrevState.Get<1>());
		}
		ParentMaterial->PostEditChange();
	}

	return PropertyBakeOutput;
}

FGLTFJsonTextureIndex FGLTFMaterialUtility::AddCombinedTexture(FGLTFConvertBuilder& Builder, const TArray<FGLTFTextureCombineSource>& CombineSources, const FIntPoint& TextureSize, const FString& TextureName, EGLTFJsonTextureFilter MinFilter, EGLTFJsonTextureFilter MagFilter, EGLTFJsonTextureWrap WrapS, EGLTFJsonTextureWrap WrapT)
{
	check(CombineSources.Num() > 0);

	TArray<FColor> Pixels;
	const EPixelFormat PixelFormat = CombineSources[0].Texture->GetPixelFormat(); // TODO: should we really assume pixel format like this?

	if (!CombineTextures(Pixels, CombineSources, TextureSize, PixelFormat))
	{
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	return AddTexture(Builder, Pixels, TextureSize, TextureName, MinFilter, MagFilter, WrapS, WrapT);
}

FGLTFJsonTextureIndex FGLTFMaterialUtility::AddTexture(FGLTFConvertBuilder& Builder, const TArray<FColor>& Pixels, const FIntPoint& TextureSize, const FString& TextureName, EGLTFJsonTextureFilter MinFilter, EGLTFJsonTextureFilter MagFilter, EGLTFJsonTextureWrap WrapS, EGLTFJsonTextureWrap WrapT)
{
	// TODO: maybe we should reuse existing samplers?
	FGLTFJsonSampler JsonSampler;
	JsonSampler.Name = TextureName;
	JsonSampler.MinFilter = MinFilter;
	JsonSampler.MagFilter = MagFilter;
	JsonSampler.WrapS = WrapS;
	JsonSampler.WrapT = WrapT;

	// TODO: reuse same texture index when image is the same
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

bool FGLTFMaterialUtility::TryGetTextureCoordinateIndex(const UMaterialExpressionTextureSample* TextureSampler, int32& TexCoord)
{
	const UMaterialExpression* Expression = TextureSampler->Coordinates.Expression;
	if (Expression == nullptr)
	{
		TexCoord = TextureSampler->ConstCoordinate;
		return true;
	}

	if (const UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression))
	{
		TexCoord = TextureCoordinate->CoordinateIndex;
		return true;
	}

	return false;
}

void FGLTFMaterialUtility::GetAllTextureCoordinateIndices(const FExpressionInput& Input, TSet<int32>& OutTexCoords)
{
	UMaterialExpression* InputExpression = Input.Expression;
	if (InputExpression == nullptr)
	{
		return;
	}

	TArray<UMaterialExpression*> Expressions;
	InputExpression->GetAllInputExpressions(Expressions);

	ExpandAllFunctionExpressions(Expressions);

	for (const UMaterialExpression* Expression : Expressions)
	{
		if (const UMaterialExpressionTextureSample* TextureSample = Cast<UMaterialExpressionTextureSample>(Expression))
		{
			if (TextureSample->Coordinates.Expression == nullptr)
			{
				OutTexCoords.Add(TextureSample->ConstCoordinate);
			}
		}
		else if (const UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression))
		{
			OutTexCoords.Add(TextureCoordinate->CoordinateIndex);
		}
	}
}

void FGLTFMaterialUtility::ExpandAllFunctionExpressions(TArray<UMaterialExpression*>& InOutExpressions)
{
	TArray<UMaterialExpression*> UnexpandedExpressions(InOutExpressions);

	for (const UMaterialExpression* UnexpandedExpression : UnexpandedExpressions)
	{
		if (const UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(UnexpandedExpression))
		{
			if (UMaterialFunctionInterface* Function = FunctionCall->MaterialFunction)
			{
				Function->GetAllExpressionsOfType<UMaterialExpression>(InOutExpressions);
			}
		}
		else if (const UMaterialExpressionMaterialAttributeLayers* AttributeLayers = Cast<UMaterialExpressionMaterialAttributeLayers>(UnexpandedExpression))
		{
			for (UMaterialFunctionInterface* Layer : AttributeLayers->GetLayers())
			{
				if (Layer != nullptr)
				{
					Layer->GetAllExpressionsOfType<UMaterialExpression>(InOutExpressions);
				}
			}

			for (UMaterialFunctionInterface* Blend : AttributeLayers->GetBlends())
			{
				if (Blend != nullptr)
				{
					Blend->GetAllExpressionsOfType<UMaterialExpression>(InOutExpressions);
				}
			}
		}
	}

	InOutExpressions.RemoveAll([](const UMaterialExpression* Expression) {
		return Expression->IsA<UMaterialExpressionMaterialFunctionCall>() || Expression->IsA<UMaterialExpressionMaterialAttributeLayers>();
	});
}
