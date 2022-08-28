// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UMaterialInterface* MaterialInterface)
{
	FGLTFJsonMaterial JsonMaterial;
	JsonMaterial.Name = Name;

	const UMaterial* Material = MaterialInterface->GetMaterial();
	JsonMaterial.PBRMetallicRoughness.BaseColorFactor = FGLTFConverterUtility::ConvertColor(GetConstantColor(Material->BaseColor));
	JsonMaterial.PBRMetallicRoughness.MetallicFactor = GetConstantScalar(Material->Metallic);
	JsonMaterial.PBRMetallicRoughness.RoughnessFactor = GetConstantScalar(Material->Roughness);
	JsonMaterial.EmissiveFactor = FGLTFConverterUtility::ConvertColor(GetConstantColor(Material->EmissiveColor));

	JsonMaterial.AlphaMode = FGLTFConverterUtility::ConvertBlendMode(Material->BlendMode);
	if (JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Blend)
	{
		JsonMaterial.PBRMetallicRoughness.BaseColorFactor.A = GetConstantScalar(Material->Opacity);
	}
	else if (JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Mask)
	{
		JsonMaterial.PBRMetallicRoughness.BaseColorFactor.A = GetConstantScalar(Material->OpacityMask);
	}

	JsonMaterial.AlphaCutoff = Material->OpacityMaskClipValue;
	JsonMaterial.DoubleSided = Material->TwoSided;

	return Builder.AddMaterial(JsonMaterial);
}

FLinearColor FGLTFMaterialConverter::GetConstantColor(const FColorMaterialInput& MaterialInput) const
{
	const UMaterialExpression* Expression = MaterialInput.Expression;
	if (Expression != nullptr)
	{
		if (const UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			return VectorParameter->DefaultValue;
		}

		if (const UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			return { ScalarParameter->DefaultValue, ScalarParameter->DefaultValue, ScalarParameter->DefaultValue };
		}

		if (const UMaterialExpressionConstant4Vector* Constant4Vector =Cast<UMaterialExpressionConstant4Vector>(Expression))
		{
			return Constant4Vector->Constant;
		}

		if (const UMaterialExpressionConstant3Vector* Constant3Vector = Cast<UMaterialExpressionConstant3Vector>(Expression))
		{
			return Constant3Vector->Constant;
		}

		if (const UMaterialExpressionConstant2Vector* Constant2Vector =Cast<UMaterialExpressionConstant2Vector>(Expression))
		{
			return { Constant2Vector->R, Constant2Vector->G, 0 }; // TODO: is this case ever possible?
		}

		if (const UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
		{
			return { Constant->R, Constant->R, Constant->R };
		}
	}

	return MaterialInput.Constant;
}

float FGLTFMaterialConverter::GetConstantScalar(const FScalarMaterialInput& MaterialInput) const
{
	const UMaterialExpression* Expression = MaterialInput.Expression;
	if (Expression != nullptr)
	{
		if (const UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			return VectorParameter->DefaultValue.R;
		}

		if (const UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			return ScalarParameter->DefaultValue;
		}

		if (const UMaterialExpressionConstant4Vector* Constant4Vector =Cast<UMaterialExpressionConstant4Vector>(Expression))
		{
			return Constant4Vector->Constant.R;
		}

		if (const UMaterialExpressionConstant3Vector* Constant3Vector = Cast<UMaterialExpressionConstant3Vector>(Expression))
		{
			return Constant3Vector->Constant.R;
		}

		if (const UMaterialExpressionConstant2Vector* Constant2Vector =Cast<UMaterialExpressionConstant2Vector>(Expression))
		{
			return Constant2Vector->R;
		}

		if (const UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
		{
			return Constant->R;
		}
	}

	return MaterialInput.Constant;
}
