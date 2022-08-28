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

	if (JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Blend || JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Mask)
	{
		const FScalarMaterialInput& OpacityInput = JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Blend ? Material->Opacity : Material->OpacityMask;
		if (!TryGetBaseColorAndOpacity(Builder, JsonMaterial.PBRMetallicRoughness, Material->BaseColor, OpacityInput, MaterialInstance))
		{
			// TODO: add fallback to material baking
		}
	}
	else
	{
		if (!TryGetConstantColor(JsonMaterial.PBRMetallicRoughness.BaseColorFactor, Material->BaseColor, MaterialInstance))
		{
			if (!TryGetSourceTexture(Builder, JsonMaterial.PBRMetallicRoughness.BaseColorTexture, Material->BaseColor, MaterialInstance))
			{
				// TODO: add fallback to material baking
			}
		}
	}

	if (!TryGetMetallicAndRoughness(Builder, JsonMaterial.PBRMetallicRoughness, Material->Metallic, Material->Roughness, MaterialInterface))
	{
		// TODO: handle failure?
	}

	if (!TryGetConstantColor(JsonMaterial.EmissiveFactor, Material->EmissiveColor, MaterialInstance))
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.EmissiveTexture, Material->EmissiveColor, MaterialInstance))
		{
			// TODO: add fallback to material baking
		}
		else
		{
			JsonMaterial.EmissiveFactor = FGLTFJsonColor3::White; // make sure texture is not multiplied with black
		}
	}

	if (Material->Normal.Expression != nullptr)
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.NormalTexture, Material->Normal, MaterialInstance))
		{
			// TODO: add fallback to material baking
		}
	}

	if (Material->AmbientOcclusion.Expression != nullptr)
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.OcclusionTexture, Material->AmbientOcclusion, MaterialInstance))
		{
			// TODO: add fallback to material baking
		}
	}

	return Builder.AddMaterial(JsonMaterial);
}

bool FGLTFMaterialConverter::TryGetBaseColorAndOpacity(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutPBRParams, const FColorMaterialInput& BaseColorInput, const FScalarMaterialInput& OpacityInput, const UMaterialInstance* MaterialInstance) const
{
	const bool bIsBaseColorConstant = TryGetConstantColor(OutPBRParams.BaseColorFactor, BaseColorInput, MaterialInstance);
	const bool bIsOpacityConstant = TryGetConstantScalar(OutPBRParams.BaseColorFactor.A, OpacityInput, MaterialInstance);

	if (bIsBaseColorConstant && bIsOpacityConstant)
	{
		return true;
	}

	const UTexture2D* BaseColorTexture;
	const UTexture2D* OpacityTexture;
	int32 BaseColorTexCoord;
	int32 OpacityTexCoord;

	const bool bHasBaseColorSourceTexture = TryGetSourceTexture(BaseColorTexture, BaseColorTexCoord, BaseColorInput, MaterialInstance);
	const bool bHasOpacitySourceTexture = TryGetSourceTexture(OpacityTexture, OpacityTexCoord, OpacityInput, MaterialInstance);

	if (bHasBaseColorSourceTexture && bHasOpacitySourceTexture)
	{
		if (BaseColorTexture == OpacityTexture && BaseColorTexCoord == OpacityTexCoord)
		{
			// TODO: make sure textures are correctly masked
			OutPBRParams.BaseColorTexture.Index = Builder.GetOrAddTexture(BaseColorTexture);
			OutPBRParams.BaseColorTexture.TexCoord = BaseColorTexCoord;
			return true;
		}

		// TODO: add support for combining two textures
		return false;
	}

	if (bHasBaseColorSourceTexture && bIsOpacityConstant)
	{
		// TODO: add support for combining constant with texture
		return false;
	}

	if (bIsBaseColorConstant && bHasOpacitySourceTexture)
	{
		// TODO: add support for combining constant with texture
		return false;
	}

	return false;
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

	const bool bHasMetallicSourceTexture = TryGetSourceTexture(MetallicTexture, MetallicTexCoord, MetallicInput, MaterialInstance);
	const bool bHasRoughnessSourceTexture = TryGetSourceTexture(RoughnessTexture, RoughnessTexCoord, RoughnessInput, MaterialInstance);

	// Detect the "happy path" where both inputs share the same texture, and both inputs correctly masked.
	if (bHasMetallicSourceTexture && bHasRoughnessSourceTexture)
	{
		const FIntVector4 MetallicMask(0, 0, 1, 0);
		const FIntVector4 RoughnessMask(0, 1, 0, 0);

		if (MetallicTexture == RoughnessTexture &&
			MetallicTexCoord == RoughnessTexCoord &&
			MetallicMask == FGLTFMaterialUtility::ConvertMaskToVector(MetallicInput) &&
			RoughnessMask == FGLTFMaterialUtility::ConvertMaskToVector(RoughnessInput))
		{
			OutPBRParams.MetallicRoughnessTexture.Index = Builder.GetOrAddTexture(MetallicTexture);
			OutPBRParams.MetallicRoughnessTexture.TexCoord = MetallicTexCoord;
			return true;
		}
	}

	int32 TexCoord = 0;
	FIntPoint TextureSize(512, 512);	// TODO: make default baking-resolution configurable
	EGLTFJsonTextureWrap TextureWrap = EGLTFJsonTextureWrap::ClampToEdge;
	EGLTFJsonTextureFilter TextureFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;
	const EPixelFormat PixelFormat = PF_B8G8R8A8;

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

	RoughnessTexture = FGLTFMaterialUtility::BakeMaterialProperty(TextureSize, MP_Roughness, MaterialInterface);
	MetallicTexture = FGLTFMaterialUtility::BakeMaterialProperty(TextureSize, MP_Metallic, MaterialInterface);

	const FGLTFJsonTextureIndex TextureIndex = FGLTFMaterialUtility::AddMetallicRoughnessTexture(
		Builder,
		MetallicTexture,
		RoughnessTexture,
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

		// TODO: add support for output mask
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

		// TODO: add support for output mask
		OutValue = Value.R;
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

bool FGLTFMaterialConverter::TryGetSourceTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, const FExpressionInput& MaterialInput, const UMaterialInstance* MaterialInstance) const
{
	const UTexture2D* Texture;
	int32 TexCoord;

	if (TryGetSourceTexture(Texture, TexCoord, MaterialInput, MaterialInstance))
	{
		// TODO: add support for output mask
		OutTexInfo.Index = Builder.GetOrAddTexture(Texture);
		OutTexInfo.TexCoord = TexCoord;
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, const FExpressionInput& MaterialInput, const UMaterialInstance* MaterialInstance) const
{
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

		// TODO: add support for output mask
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

		// TODO: add support for output mask
		return true;
	}

	return false;
}
