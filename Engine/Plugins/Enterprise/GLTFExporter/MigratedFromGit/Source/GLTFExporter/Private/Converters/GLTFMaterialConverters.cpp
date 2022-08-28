// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
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
		if (!TryGetSimpleBaseColorOpacity(Builder, JsonMaterial, Material->BaseColor, OpacityInput, MaterialInstance))
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

	if (!TryGetSimpleMetallicRoughness(Builder, JsonMaterial.PBRMetallicRoughness, Material->Metallic, Material->Roughness, MaterialInstance))
	{
		// TODO: add fallback to material baking
	}

	if (!TryGetConstantColor(JsonMaterial.EmissiveFactor, Material->EmissiveColor, MaterialInstance))
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.EmissiveTexture, Material->EmissiveColor, MaterialInstance))
		{
			// TODO: add fallback to material baking
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

bool FGLTFMaterialConverter::TryGetSimpleBaseColorOpacity(FGLTFConvertBuilder& Builder, FGLTFJsonMaterial& OutValue, const FColorMaterialInput& BaseColorInput, const FScalarMaterialInput& OpacityInput, const UMaterialInstance* MaterialInstance) const
{
	const bool bIsBaseColorConstant = TryGetConstantColor(OutValue.PBRMetallicRoughness.BaseColorFactor, BaseColorInput, MaterialInstance);
	const bool bIsOpacityConstant = TryGetConstantScalar(OutValue.PBRMetallicRoughness.BaseColorFactor.A, OpacityInput, MaterialInstance);

	if (bIsBaseColorConstant && bIsOpacityConstant)
	{
		return true;
	}

	// TODO: add support for other combos
	return false;
}

bool FGLTFMaterialConverter::TryGetSimpleMetallicRoughness(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutValue, const FScalarMaterialInput& MetallicInput, const FScalarMaterialInput& RoughnessInput, const UMaterialInstance* MaterialInstance) const
{
	const bool bIsMetallicConstant = TryGetConstantScalar(OutValue.MetallicFactor, MetallicInput, MaterialInstance);
	const bool bIsRoughnessConstant = TryGetConstantScalar(OutValue.RoughnessFactor, RoughnessInput, MaterialInstance);

	if (bIsMetallicConstant && bIsRoughnessConstant)
	{
		return true;
	}

	// TODO: add support for other combos
	return false;
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
		// TODO: add support for output mask
		OutValue = Constant4Vector->Constant;
		return true;
	}

	if (const UMaterialExpressionConstant3Vector* Constant3Vector = ExactCast<UMaterialExpressionConstant3Vector>(Expression))
	{
		// TODO: add support for output mask
		OutValue = Constant3Vector->Constant;
		return true;
	}

	if (const UMaterialExpressionConstant2Vector* Constant2Vector = ExactCast<UMaterialExpressionConstant2Vector>(Expression))
	{
		// TODO: add support for output mask
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
		// TODO: add support for output mask
		OutValue = Constant4Vector->Constant.R;
		return true;
	}

	if (const UMaterialExpressionConstant3Vector* Constant3Vector = ExactCast<UMaterialExpressionConstant3Vector>(Expression))
	{
		// TODO: add support for output mask
		OutValue = Constant3Vector->Constant.R;
		return true;
	}

	if (const UMaterialExpressionConstant2Vector* Constant2Vector = ExactCast<UMaterialExpressionConstant2Vector>(Expression))
	{
		// TODO: add support for output mask
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

bool FGLTFMaterialConverter::TryGetSourceTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutValue, const FExpressionInput& MaterialInput, const UMaterialInstance* MaterialInstance) const
{
	const UTexture2D* Texture;
	int32 TexCoord;
	int32 Mask;

	if (TryGetSourceTexture(Texture, TexCoord, Mask, MaterialInput, MaterialInstance))
	{
		// TODO: add support for output mask
		OutValue.Index = Builder.GetOrAddTexture(Texture);
		OutValue.TexCoord = TexCoord;
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, int32& OutMask, const FExpressionInput& MaterialInput, const UMaterialInstance* MaterialInstance) const
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
