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

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UMaterialInterface* MaterialInterface)
{
	FGLTFJsonMaterial JsonMaterial;
	JsonMaterial.Name = Name;

	// TODO: add support for different shading models (Default Lit, Unlit, Clear Coat)

	// TODO: add support for additional blend modes (at least Additive and Modulate)
	JsonMaterial.AlphaMode = FGLTFConverterUtility::ConvertBlendMode(MaterialInterface->GetBlendMode());

	JsonMaterial.AlphaCutoff = MaterialInterface->GetOpacityMaskClipValue();
	JsonMaterial.DoubleSided = MaterialInterface->IsTwoSided();

	const UMaterial* Material = MaterialInterface->GetMaterial();
	const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);

	if (!TryGetConstantColor(JsonMaterial.PBRMetallicRoughness.BaseColorFactor, Material->BaseColor, MaterialInstance))
	{
		// TODO: temporary fallback until texture and baking support
		JsonMaterial.PBRMetallicRoughness.BaseColorFactor = FGLTFConverterUtility::ConvertColor(FLinearColor(Material->BaseColor.Constant));
	}

	if (JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Blend || JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Mask)
	{
		const FScalarMaterialInput& OpacityInput = JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Blend ? Material->Opacity : Material->OpacityMask;
		if (!TryGetConstantScalar(JsonMaterial.PBRMetallicRoughness.BaseColorFactor.A, OpacityInput, MaterialInstance))
		{
			// TODO: temporary fallback until texture and baking support
			JsonMaterial.PBRMetallicRoughness.BaseColorFactor.A = OpacityInput.Constant;
		}
	}

	if (!TryGetConstantScalar(JsonMaterial.PBRMetallicRoughness.MetallicFactor, Material->Metallic, MaterialInstance))
	{
		// TODO: temporary fallback until texture and baking support
		JsonMaterial.PBRMetallicRoughness.MetallicFactor = Material->Metallic.Constant;
	}

	if (!TryGetConstantScalar(JsonMaterial.PBRMetallicRoughness.RoughnessFactor, Material->Roughness, MaterialInstance))
	{
		// TODO: temporary fallback until texture and baking support
		JsonMaterial.PBRMetallicRoughness.RoughnessFactor = Material->Roughness.Constant;
	}

	if (!TryGetConstantColor(JsonMaterial.EmissiveFactor, Material->EmissiveColor, MaterialInstance))
	{
		// TODO: temporary fallback until texture and baking support
		JsonMaterial.EmissiveFactor = FGLTFConverterUtility::ConvertColor(FLinearColor(Material->EmissiveColor.Constant));
	}

	return Builder.AddMaterial(JsonMaterial);
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
		OutValue = MaterialInput.Constant;
		return true;
	}

	if (const UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		FLinearColor Value = VectorParameter->DefaultValue;

		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(VectorParameter->GetParameterName());
			if (!MaterialInstance->GetVectorParameterValue(ParameterInfo, Value))
			{
				// TODO: should not happen
			}
		}

		OutValue = Value;
		return true;
	}

	if (const UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
	{
		float Value = ScalarParameter->DefaultValue;

		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(ScalarParameter->GetParameterName());
			if (!MaterialInstance->GetScalarParameterValue(ParameterInfo, Value))
			{
				// TODO: should not happen
			}
		}

		OutValue = { Value, Value, Value, Value };
		return true;
	}

	if (const UMaterialExpressionConstant4Vector* Constant4Vector =Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		OutValue = Constant4Vector->Constant;
		return true;
	}

	if (const UMaterialExpressionConstant3Vector* Constant3Vector = Cast<UMaterialExpressionConstant3Vector>(Expression))
	{
		OutValue = Constant3Vector->Constant;
		return true;
	}

	if (const UMaterialExpressionConstant2Vector* Constant2Vector =Cast<UMaterialExpressionConstant2Vector>(Expression))
	{
		OutValue = { Constant2Vector->R, Constant2Vector->G, 0, 0 }; // TODO: is this case ever possible?
		return true;
	}

	if (const UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
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
		return false;
	}

	if (const UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		FLinearColor Value = VectorParameter->DefaultValue;

		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(VectorParameter->GetParameterName());
			if (!MaterialInstance->GetVectorParameterValue(ParameterInfo, Value))
			{
				// TODO: should not happen
			}
		}

		OutValue = Value.R;
		return true;
	}

	if (const UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
	{
		float Value = ScalarParameter->DefaultValue;

		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(ScalarParameter->GetParameterName());
			if (!MaterialInstance->GetScalarParameterValue(ParameterInfo, Value))
			{
				// TODO: should not happen
			}
		}

		OutValue = Value;
		return true;
	}

	if (const UMaterialExpressionConstant4Vector* Constant4Vector =Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		OutValue = Constant4Vector->Constant.R;
		return true;
	}

	if (const UMaterialExpressionConstant3Vector* Constant3Vector = Cast<UMaterialExpressionConstant3Vector>(Expression))
	{
		OutValue = Constant3Vector->Constant.R;
		return true;
	}

	if (const UMaterialExpressionConstant2Vector* Constant2Vector =Cast<UMaterialExpressionConstant2Vector>(Expression))
	{
		OutValue = Constant2Vector->R; // TODO: is this case ever possible?
		return true;
	}

	if (const UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
	{
		OutValue = Constant->R;
		return true;
	}

	return false;
}
