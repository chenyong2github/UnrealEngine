// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UMaterialInterface* MaterialInterface)
{
	const UMaterial* Material = MaterialInterface->GetMaterial();
	const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);

	FGLTFJsonMaterial JsonMaterial;
	JsonMaterial.Name = Name;
	// TODO: add support for different shading models (Default Lit, Unlit, Clear Coat)

	// TODO: add support for additional blend modes (at least Additive and Modulate)
	JsonMaterial.AlphaMode = FGLTFConverterUtility::ConvertBlendMode(MaterialInterface->GetBlendMode());
	JsonMaterial.AlphaCutoff = MaterialInterface->GetOpacityMaskClipValue();
	JsonMaterial.DoubleSided = MaterialInterface->IsTwoSided();

	const FLinearColor RgbaMask(1.0f, 1.0f, 1.0f, 1.0f);
	const FLinearColor RgbMask(1.0f, 1.0f, 1.0f, 0.0f);
	const FLinearColor RMask(1.0f, 0.0f, 0.0f, 0.0f);

	if (JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Blend || JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Mask)
	{
		const FScalarMaterialInput& OpacityInput = JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Blend ? Material->Opacity : Material->OpacityMask;
		if (!TryGetBaseColorAndOpacity(Builder, JsonMaterial.PBRMetallicRoughness, Material->BaseColor, OpacityInput, MaterialInterface))
		{
			// TODO: handle failure?
		}
	}
	else
	{
		if (!TryGetConstantColor(JsonMaterial.PBRMetallicRoughness.BaseColorFactor, Material->BaseColor, MaterialInstance))
		{
			if (!TryGetSourceTexture(Builder, JsonMaterial.PBRMetallicRoughness.BaseColorTexture, Material->BaseColor, MaterialInstance, {RgbaMask, RgbMask}))
			{
				if (!TryGetBakedTexture(Builder, JsonMaterial.PBRMetallicRoughness.BaseColorTexture, MP_BaseColor, MaterialInterface))
				{
					// TODO: handle failure?
				}
			}
		}
	}

	if (!TryGetMetallicAndRoughness(Builder, JsonMaterial.PBRMetallicRoughness, Material->Metallic, Material->Roughness, MaterialInterface))
	{
		// TODO: handle failure?
	}

	if (!TryGetConstantColor(JsonMaterial.EmissiveFactor, Material->EmissiveColor, MaterialInstance))
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.EmissiveTexture, Material->EmissiveColor, MaterialInstance, {RgbaMask, RgbMask}))
		{
			if (!TryGetBakedTexture(Builder, JsonMaterial.EmissiveTexture, MP_EmissiveColor, MaterialInterface))
			{
				// TODO: handle failure?
			}
		}
		else
		{
			JsonMaterial.EmissiveFactor = FGLTFJsonColor3::White; // make sure texture is not multiplied with black
		}
	}

	if (Material->Normal.Expression != nullptr)
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.NormalTexture, Material->Normal, MaterialInstance, {RgbaMask, RgbMask}))
		{
			if (!TryGetBakedTexture(Builder, JsonMaterial.NormalTexture, MP_Normal, MaterialInterface))
			{
				// TODO: handle failure?
			}
		}
	}

	if (Material->AmbientOcclusion.Expression != nullptr)
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.OcclusionTexture, Material->AmbientOcclusion, MaterialInstance, {RMask}))
		{
			if (!TryGetBakedTexture(Builder, JsonMaterial.OcclusionTexture, MP_AmbientOcclusion, MaterialInterface))
			{
				// TODO: handle failure?
			}
		}
	}

	return Builder.AddMaterial(JsonMaterial);
}

bool FGLTFMaterialConverter::TryGetBaseColorAndOpacity(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutPBRParams, const FColorMaterialInput& BaseColorInput, const FScalarMaterialInput& OpacityInput, const UMaterialInterface* MaterialInterface) const
{
	const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);

	const bool bIsBaseColorConstant = TryGetConstantColor(OutPBRParams.BaseColorFactor, BaseColorInput, MaterialInstance);
	const bool bIsOpacityConstant = TryGetConstantScalar(OutPBRParams.BaseColorFactor.A, OpacityInput, MaterialInstance);

	if (bIsBaseColorConstant && bIsOpacityConstant)
	{
		return true;
	}

	// NOTE: since we always bake the properties (for now) when atleast property is non-const, we need
	// to reset the constant factors to their defaults. Otherwise the baked value of a constant property
	// would be scaled with the factor, i.e a double scaling.
	OutPBRParams.BaseColorFactor = {1, 1, 1, 1};

	const UTexture2D* BaseColorTexture;
	const UTexture2D* OpacityTexture;
	int32 BaseColorTexCoord;
	int32 OpacityTexCoord;

	const FLinearColor BaseColorMask(1, 1, 1, 0);
	const FLinearColor OpacityMask(0, 0, 0, 1);

	const bool bHasBaseColorSourceTexture = TryGetSourceTexture(BaseColorTexture, BaseColorTexCoord, BaseColorInput, MaterialInstance, {BaseColorMask});
	const bool bHasOpacitySourceTexture = TryGetSourceTexture(OpacityTexture, OpacityTexCoord, OpacityInput, MaterialInstance, {OpacityMask});

	// Detect the "happy path" where both inputs share the same texture and are correctly masked.
	if (bHasBaseColorSourceTexture &&
		bHasOpacitySourceTexture &&
		BaseColorTexture == OpacityTexture &&
		BaseColorTexCoord == OpacityTexCoord)
	{
		OutPBRParams.BaseColorTexture.Index = Builder.GetOrAddTexture(BaseColorTexture);
		OutPBRParams.BaseColorTexture.TexCoord = BaseColorTexCoord;
		return true;
	}

	// TODO: make default baking-resolution configurable
	// TODO: add support for detecting the correct tex-coord for this property based on connected nodes
	// TODO: add support for calculating the ideal resolution to use for baking based on connected (texture) nodes
	int32 TexCoord = 0;
	FIntPoint TextureSize(512, 512);

	EGLTFJsonTextureWrap TextureWrap = EGLTFJsonTextureWrap::ClampToEdge;
	EGLTFJsonTextureFilter TextureFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;

	if (bHasBaseColorSourceTexture && bHasOpacitySourceTexture)
	{
		const bool bAreTexturesCompatible = BaseColorTexCoord == OpacityTexCoord &&
			BaseColorTexture->AddressX == OpacityTexture->AddressX &&
			BaseColorTexture->AddressY == OpacityTexture->AddressY;

		if (!bAreTexturesCompatible)
		{
			// TODO: handle differences in wrapping or uv-coords
			return false;
		}

		TexCoord = BaseColorTexCoord;
		TextureSize = FIntPoint(
			FMath::Max(BaseColorTexture->GetSizeX(), OpacityTexture->GetSizeX()),
			FMath::Max(BaseColorTexture->GetSizeY(), OpacityTexture->GetSizeY()));

		TextureWrap = FGLTFConverterUtility::ConvertWrap(BaseColorTexture->AddressX);
		TextureFilter = FGLTFConverterUtility::ConvertFilter(BaseColorTexture->Filter, BaseColorTexture->LODGroup);
	}
	else if (bHasBaseColorSourceTexture)
	{
		TexCoord = BaseColorTexCoord;
		TextureSize = FIntPoint(BaseColorTexture->GetSizeX(), BaseColorTexture->GetSizeY());
		TextureWrap = FGLTFConverterUtility::ConvertWrap(BaseColorTexture->AddressX);
		TextureFilter = FGLTFConverterUtility::ConvertFilter(BaseColorTexture->Filter, BaseColorTexture->LODGroup);
	}
	else if (bHasOpacitySourceTexture)
	{
		TexCoord = OpacityTexCoord;
		TextureSize = FIntPoint(OpacityTexture->GetSizeX(), OpacityTexture->GetSizeY());
		TextureWrap = FGLTFConverterUtility::ConvertWrap(OpacityTexture->AddressX);
		TextureFilter = FGLTFConverterUtility::ConvertFilter(OpacityTexture->Filter, OpacityTexture->LODGroup);
	}

	const EMaterialProperty OpacityProperty = MaterialInterface->GetBlendMode() == BLEND_Masked ? MP_OpacityMask : MP_Opacity;

	BaseColorTexture = FGLTFMaterialUtility::BakeMaterialPropertyToTexture(TextureSize, MP_BaseColor, MaterialInterface);
	OpacityTexture = FGLTFMaterialUtility::BakeMaterialPropertyToTexture(TextureSize, OpacityProperty, MaterialInterface, true);

	// NOTE: the baked textures may have a different (smaller) size than requested, so we update the
	// texture-size to fit both textures without wasting too much space.
	TextureSize = {
		FMath::Max(BaseColorTexture->GetSizeX(), OpacityTexture->GetSizeX()),
		FMath::Max(BaseColorTexture->GetSizeY(), OpacityTexture->GetSizeY())
	};

	// TODO: handle the case where TextureSize is 1x1. In this case, both properties are constants and we should
	// extract the value of each property from the texture and use as-is instead of exporting a combined texture.

	const FString TextureName = FString::Printf(TEXT("%s_BaseColor"), *MaterialInterface->GetName());

	const TArray<FGLTFTextureCombineSource> CombineSources =
	{
		{OpacityTexture, OpacityMask, SE_BLEND_Opaque},
		{BaseColorTexture, BaseColorMask}
	};

	const FGLTFJsonTextureIndex TextureIndex = FGLTFMaterialUtility::AddCombinedTexture(
		Builder,
		CombineSources,
		TextureSize,
		TextureName,
		TextureFilter,
		TextureWrap);

	OutPBRParams.BaseColorTexture.TexCoord = TexCoord;
	OutPBRParams.BaseColorTexture.Index = TextureIndex;

	return true;
}

bool FGLTFMaterialConverter::TryGetMetallicAndRoughness(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutPBRParams, const FScalarMaterialInput& MetallicInput, const FScalarMaterialInput& RoughnessInput, const UMaterialInterface* MaterialInterface) const
{
	const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);

	const bool bIsMetallicConstant = TryGetConstantScalar(OutPBRParams.MetallicFactor, MetallicInput, MaterialInstance);
	const bool bIsRoughnessConstant = TryGetConstantScalar(OutPBRParams.RoughnessFactor, RoughnessInput, MaterialInstance);

	if (bIsMetallicConstant && bIsRoughnessConstant)
	{
		return true;
	}

	// NOTE: since we always bake the properties (for now) when atleast property is non-const, we need
	// to reset the constant factors to their defaults. Otherwise the baked value of a constant property
	// would be scaled with the factor, i.e a double scaling.
	OutPBRParams.MetallicFactor = 1;
	OutPBRParams.RoughnessFactor = 1;

	const UTexture2D* MetallicTexture;
	const UTexture2D* RoughnessTexture;
	int32 MetallicTexCoord;
	int32 RoughnessTexCoord;

	const FLinearColor MetallicMask(0, 0, 1, 0);
	const FLinearColor RoughnessMask(0, 1, 0, 0);

	const bool bHasMetallicSourceTexture = TryGetSourceTexture(MetallicTexture, MetallicTexCoord, MetallicInput, MaterialInstance, {MetallicMask});
	const bool bHasRoughnessSourceTexture = TryGetSourceTexture(RoughnessTexture, RoughnessTexCoord, RoughnessInput, MaterialInstance, {RoughnessMask});

	// Detect the "happy path" where both inputs share the same texture and are correctly masked.
	if (bHasMetallicSourceTexture &&
		bHasRoughnessSourceTexture &&
		MetallicTexture == RoughnessTexture &&
		MetallicTexCoord == RoughnessTexCoord)
	{
		OutPBRParams.MetallicRoughnessTexture.Index = Builder.GetOrAddTexture(MetallicTexture);
		OutPBRParams.MetallicRoughnessTexture.TexCoord = MetallicTexCoord;
		return true;
	}

	// TODO: make default baking-resolution configurable
	// TODO: add support for detecting the correct tex-coord for this property based on connected nodes
	// TODO: add support for calculating the ideal resolution to use for baking based on connected (texture) nodes
	int32 TexCoord = 0;
	FIntPoint TextureSize(512, 512);

	EGLTFJsonTextureWrap TextureWrap = EGLTFJsonTextureWrap::ClampToEdge;
	EGLTFJsonTextureFilter TextureFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;

	if (bHasMetallicSourceTexture && bHasRoughnessSourceTexture)
	{
		const bool bAreTexturesCompatible = MetallicTexCoord == RoughnessTexCoord &&
			MetallicTexture->AddressX == RoughnessTexture->AddressX &&
			MetallicTexture->AddressY == RoughnessTexture->AddressY;

		if (!bAreTexturesCompatible)
		{
			// TODO: handle differences in wrapping or uv-coords
			return false;
		}

		TexCoord = MetallicTexCoord;
		TextureSize = FIntPoint(
			FMath::Max(MetallicTexture->GetSizeX(), RoughnessTexture->GetSizeX()),
			FMath::Max(MetallicTexture->GetSizeY(), RoughnessTexture->GetSizeY()));

		TextureWrap = FGLTFConverterUtility::ConvertWrap(MetallicTexture->AddressX);
		TextureFilter = FGLTFConverterUtility::ConvertFilter(MetallicTexture->Filter, MetallicTexture->LODGroup);
	}
	else if (bHasMetallicSourceTexture)
	{
		TexCoord = MetallicTexCoord;
		TextureSize = FIntPoint(MetallicTexture->GetSizeX(), MetallicTexture->GetSizeY());
		TextureWrap = FGLTFConverterUtility::ConvertWrap(MetallicTexture->AddressX);
		TextureFilter = FGLTFConverterUtility::ConvertFilter(MetallicTexture->Filter, MetallicTexture->LODGroup);
	}
	else if (bHasRoughnessSourceTexture)
	{
		TexCoord = RoughnessTexCoord;
		TextureSize = FIntPoint(RoughnessTexture->GetSizeX(), RoughnessTexture->GetSizeY());
		TextureWrap = FGLTFConverterUtility::ConvertWrap(RoughnessTexture->AddressX);
		TextureFilter = FGLTFConverterUtility::ConvertFilter(RoughnessTexture->Filter, RoughnessTexture->LODGroup);
	}

	RoughnessTexture = FGLTFMaterialUtility::BakeMaterialPropertyToTexture(TextureSize, MP_Roughness, MaterialInterface);
	MetallicTexture = FGLTFMaterialUtility::BakeMaterialPropertyToTexture(TextureSize, MP_Metallic, MaterialInterface);

	// NOTE: the baked textures may have a different (smaller) size than requested, so we update the
	// texture-size to fit both textures without wasting too much space.
	TextureSize = {
		FMath::Max(MetallicTexture->GetSizeX(), RoughnessTexture->GetSizeX()),
		FMath::Max(MetallicTexture->GetSizeY(), RoughnessTexture->GetSizeY())
	};

	// TODO: handle the case where TextureSize is 1x1. In this case, both properties are constants and we should
	// extract the value of each property from the texture and use as-is instead of exporting a combined texture.

	const FString TextureName = FString::Printf(TEXT("%s_MetallicRoughness"), *MaterialInterface->GetName());

	const TArray<FGLTFTextureCombineSource> CombineSources =
	{
		{MetallicTexture, MetallicMask},
		{RoughnessTexture, RoughnessMask}
	};

	const FGLTFJsonTextureIndex TextureIndex = FGLTFMaterialUtility::AddCombinedTexture(
		Builder,
		CombineSources,
		TextureSize,
		TextureName,
		TextureFilter,
		TextureWrap);

	OutPBRParams.MetallicRoughnessTexture.TexCoord = TexCoord;
	OutPBRParams.MetallicRoughnessTexture.Index = TextureIndex;

	return true;
}

bool FGLTFMaterialConverter::TryGetConstantColor(FGLTFJsonColor3& OutValue, const FColorMaterialInput& MaterialInput, const UMaterialInstance* MaterialInstance) const
{
	FLinearColor Value;
	if (TryGetConstantColor(Value, MaterialInput, MaterialInstance))
	{
		OutValue = FGLTFConverterUtility::ConvertColor(Value);
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetConstantColor(FGLTFJsonColor4& OutValue, const FColorMaterialInput& MaterialInput, const UMaterialInstance* MaterialInstance) const
{
	FLinearColor Value;
	if (TryGetConstantColor(Value, MaterialInput, MaterialInstance))
	{
		OutValue = FGLTFConverterUtility::ConvertColor(Value);
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetConstantColor(FLinearColor& OutValue, const FColorMaterialInput& MaterialInput, const UMaterialInstance* MaterialInstance) const
{
	// TODO: handle emissive color-values above 1.0

	const UMaterialExpression* Expression = MaterialInput.Expression;
	if (Expression == nullptr)
	{
		// TODO: is this assumption correct?
		OutValue = FLinearColor(MaterialInput.Constant);
		return true;
	}

	if (const UMaterialExpressionVectorParameter* VectorParameter = ExactCast<UMaterialExpressionVectorParameter>(Expression))
	{
		FLinearColor Value = VectorParameter->DefaultValue;

		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(VectorParameter->GetParameterName());
			if (!MaterialInstance->GetVectorParameterValue(ParameterInfo, Value))
			{
				// TODO: can this happen?
			}
		}

		const uint32 MaskComponentCount = FGLTFMaterialUtility::GetMaskComponentCount(MaterialInput);

		if (MaskComponentCount > 0)
		{
			const FLinearColor Mask = FGLTFMaterialUtility::GetMask(MaterialInput);

			Value *= Mask;

			if (MaskComponentCount == 1)
			{
				const float ComponentValue = Value.R + Value.G + Value.B + Value.A;
				Value = { ComponentValue, ComponentValue, ComponentValue, ComponentValue };
			}
		}

		OutValue = Value;
		return true;
	}

	if (const UMaterialExpressionScalarParameter* ScalarParameter = ExactCast<UMaterialExpressionScalarParameter>(Expression))
	{
		float Value = ScalarParameter->DefaultValue;

		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(ScalarParameter->GetParameterName());
			if (!MaterialInstance->GetScalarParameterValue(ParameterInfo, Value))
			{
				// TODO: can this happen?
			}
		}

		OutValue = { Value, Value, Value, Value };
		return true;
	}

	if (const UMaterialExpressionConstant4Vector* Constant4Vector = ExactCast<UMaterialExpressionConstant4Vector>(Expression))
	{
		OutValue = Constant4Vector->Constant;
		return true;
	}

	if (const UMaterialExpressionConstant3Vector* Constant3Vector = ExactCast<UMaterialExpressionConstant3Vector>(Expression))
	{
		OutValue = Constant3Vector->Constant;
		return true;
	}

	if (const UMaterialExpressionConstant2Vector* Constant2Vector = ExactCast<UMaterialExpressionConstant2Vector>(Expression))
	{
		OutValue = { Constant2Vector->R, Constant2Vector->G, 0, 0 };
		return true;
	}

	if (const UMaterialExpressionConstant* Constant = ExactCast<UMaterialExpressionConstant>(Expression))
	{
		OutValue = { Constant->R, Constant->R, Constant->R, Constant->R };
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetConstantScalar(float& OutValue, const FScalarMaterialInput& MaterialInput, const UMaterialInstance* MaterialInstance) const
{
	const UMaterialExpression* Expression = MaterialInput.Expression;
	if (Expression == nullptr)
	{
		// TODO: is this assumption correct?
		OutValue = MaterialInput.Constant;
		return true;
	}

	if (const UMaterialExpressionVectorParameter* VectorParameter = ExactCast<UMaterialExpressionVectorParameter>(Expression))
	{
		FLinearColor Value = VectorParameter->DefaultValue;

		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(VectorParameter->GetParameterName());
			if (!MaterialInstance->GetVectorParameterValue(ParameterInfo, Value))
			{
				// TODO: can this happen?
			}
		}

		const uint32 MaskComponentCount = FGLTFMaterialUtility::GetMaskComponentCount(MaterialInput);

		if (MaskComponentCount > 0)
		{
			const FLinearColor Mask = FGLTFMaterialUtility::GetMask(MaterialInput);
			Value *= Mask;
		}

		// TODO: is this a correct assumption, that the max component should be used as value?
		OutValue = Value.GetMax();
		return true;
	}

	if (const UMaterialExpressionScalarParameter* ScalarParameter = ExactCast<UMaterialExpressionScalarParameter>(Expression))
	{
		float Value = ScalarParameter->DefaultValue;

		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(ScalarParameter->GetParameterName());
			if (!MaterialInstance->GetScalarParameterValue(ParameterInfo, Value))
			{
				// TODO: can this happen?
			}
		}

		OutValue = Value;
		return true;
	}

	if (const UMaterialExpressionConstant4Vector* Constant4Vector = ExactCast<UMaterialExpressionConstant4Vector>(Expression))
	{
		OutValue = Constant4Vector->Constant.R;
		return true;
	}

	if (const UMaterialExpressionConstant3Vector* Constant3Vector = ExactCast<UMaterialExpressionConstant3Vector>(Expression))
	{
		OutValue = Constant3Vector->Constant.R;
		return true;
	}

	if (const UMaterialExpressionConstant2Vector* Constant2Vector = ExactCast<UMaterialExpressionConstant2Vector>(Expression))
	{
		OutValue = Constant2Vector->R;
		return true;
	}

	if (const UMaterialExpressionConstant* Constant = ExactCast<UMaterialExpressionConstant>(Expression))
	{
		OutValue = Constant->R;
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetBakedTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, EMaterialProperty MaterialProperty, const UMaterialInterface* MaterialInterface) const
{
	// TODO: make default baking-resolution configurable
	// TODO: add support for detecting the correct tex-coord for this property based on connected nodes
	// TODO: add support for calculating the ideal resolution to use for baking based on connected (texture) nodes
	const FIntPoint TextureSize(512, 512);
	const uint32 TexCoord = 0;

	const FGLTFPropertyBakeOutput PropertyBakeOutput = FGLTFMaterialUtility::BakeMaterialProperty(
		TextureSize,
		MaterialProperty,
		MaterialInterface);

	// TODO: handle cases where PropertyBakeOutput.EmissiveScale is not 1.0 (when baking EmissiveColor)
	// TODO: handle the case where TextureSize is 1x1, which means is could be stored in a constant instead of in a texture

	TCHAR* PropertyName;

	switch (MaterialProperty)
	{
		case MP_BaseColor: PropertyName = TEXT("BaseColor"); break;
		case MP_Normal: PropertyName = TEXT("Normal"); break;
		case MP_EmissiveColor: PropertyName = TEXT("EmissiveColor"); break;
		case MP_AmbientOcclusion: PropertyName = TEXT("AmbientOcclusion"); break;
		default:
			// TODO: support for more properties
			return false;
	}

	const FString TextureName = FString::Printf(TEXT("%s_%s"), *MaterialInterface->GetName(), PropertyName);

	// TODO: support for other wrapping / filters?
	const EGLTFJsonTextureWrap TextureWrap = EGLTFJsonTextureWrap::ClampToEdge;
	const EGLTFJsonTextureFilter TextureFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;

	const FGLTFJsonTextureIndex TextureIndex = FGLTFMaterialUtility::AddTexture(
		Builder,
		PropertyBakeOutput.Pixels,
		PropertyBakeOutput.Size,
		TextureName,
		PropertyBakeOutput.PixelFormat,
		TextureFilter,
		TextureWrap);

	OutTexInfo.TexCoord = TexCoord;
	OutTexInfo.Index = TextureIndex;

	return true;
}

bool FGLTFMaterialConverter::TryGetSourceTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, const FExpressionInput& MaterialInput, const UMaterialInstance* MaterialInstance, const TArray<FLinearColor>& AllowedMasks) const
{
	const UTexture2D* Texture;
	int32 TexCoord;

	if (TryGetSourceTexture(Texture, TexCoord, MaterialInput, MaterialInstance, AllowedMasks))
	{
		OutTexInfo.Index = Builder.GetOrAddTexture(Texture);
		OutTexInfo.TexCoord = TexCoord;
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, const FExpressionInput& MaterialInput, const UMaterialInstance* MaterialInstance, const TArray<FLinearColor>& AllowedMasks) const
{
	const FLinearColor InputMask = FGLTFMaterialUtility::GetMask(MaterialInput);
	if (AllowedMasks.Num() > 0 && !AllowedMasks.Contains(InputMask))
	{
		return false;
	}

	const UMaterialExpression* Expression = MaterialInput.Expression;
	if (Expression == nullptr)
	{
		return false;
	}

	if (const UMaterialExpressionTextureSampleParameter2D* TextureParameter = ExactCast<UMaterialExpressionTextureSampleParameter2D>(Expression))
	{
		if (MaterialInstance != nullptr)
		{
			UTexture* ParameterValue;

			const FHashedMaterialParameterInfo ParameterInfo(TextureParameter->GetParameterName());
			if (!MaterialInstance->GetTextureParameterValue(ParameterInfo, ParameterValue))
			{
				// TODO: can this happen?
			}

			OutTexture = Cast<UTexture2D>(ParameterValue);
		}

		if (OutTexture == nullptr)
		{
			OutTexture = Cast<UTexture2D>(TextureParameter->Texture);
		}

		if (OutTexture == nullptr)
		{
			// TODO: report material as broken
			return false;
		}

		// TODO: add support for texture coordinate input expression
		OutTexCoord = TextureParameter->ConstCoordinate;

		return true;
	}

	if (const UMaterialExpressionTextureSample* TextureSample = ExactCast<UMaterialExpressionTextureSample>(Expression))
	{
		// TODO: add support for texture object input expression
		OutTexture = Cast<UTexture2D>(TextureSample->Texture);

		if (OutTexture == nullptr)
		{
			if (TextureSample->Texture == nullptr)
			{
				// TODO: report material as broken
			}
			else
			{
				// TODO: report incorrect texture type
			}
			return false;
		}

		// TODO: add support for texture coordinate input expression
		OutTexCoord = TextureSample->ConstCoordinate;

		return true;
	}

	return false;
}
