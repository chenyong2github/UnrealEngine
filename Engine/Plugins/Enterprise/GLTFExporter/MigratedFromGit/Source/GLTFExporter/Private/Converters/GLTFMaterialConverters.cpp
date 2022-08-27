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

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UMaterialInterface* Material)
{
	FGLTFJsonMaterial JsonMaterial;
	JsonMaterial.Name = Name;
	// TODO: add support for different shading models (Default Lit, Unlit, Clear Coat)

	// TODO: add support for additional blend modes (like Additive and Modulate)?
	JsonMaterial.AlphaMode = FGLTFConverterUtility::ConvertBlendMode(Material->GetBlendMode());
	JsonMaterial.AlphaCutoff = Material->GetOpacityMaskClipValue();
	JsonMaterial.DoubleSided = Material->IsTwoSided();

	const FLinearColor RgbaMask(1.0f, 1.0f, 1.0f, 1.0f);
	const FLinearColor RgbMask(1.0f, 1.0f, 1.0f, 0.0f);
	const FLinearColor RMask(1.0f, 0.0f, 0.0f, 0.0f);

	// TODO: check if a property is active before trying to get it (i.e. Material->IsPropertyActive)

	if (JsonMaterial.AlphaMode == EGLTFJsonAlphaMode::Opaque)
	{
		if (!TryGetConstantColor(JsonMaterial.PBRMetallicRoughness.BaseColorFactor, MP_BaseColor, Material))
		{
			if (!TryGetSourceTexture(Builder, JsonMaterial.PBRMetallicRoughness.BaseColorTexture, MP_BaseColor, Material, { RgbaMask, RgbMask }))
			{
				if (!TryGetBakedMaterialProperty(Builder, JsonMaterial.PBRMetallicRoughness.BaseColorTexture, JsonMaterial.PBRMetallicRoughness.BaseColorFactor, MP_BaseColor, Material))
				{
					Builder.AddWarningMessage(TEXT("Failed to export BaseColor for material ") + Material->GetName());
				}
			}
		}

		JsonMaterial.PBRMetallicRoughness.BaseColorFactor.A = 1.0f; // make sure base color is opaque
	}
	else
	{
		if (!TryGetBaseColorAndOpacity(Builder, JsonMaterial.PBRMetallicRoughness, Material))
		{
			Builder.AddWarningMessage(TEXT("Failed to export BaseColor & Opacity for material ") + Material->GetName());
		}
	}

	if (!TryGetMetallicAndRoughness(Builder, JsonMaterial.PBRMetallicRoughness, Material))
	{
		Builder.AddWarningMessage(TEXT("Failed to export Metallic & Roughness for material ") + Material->GetName());
	}

	// NOTE: export of EmissiveColor has been temporarily disabled because of visual differences that
	// are not solvable in the scope of MR !19.
	// The issues revolve mainly around very bright emission in the exported materials compared to
	// how the same materials look inside of UE.
	// TODO: solve the issues in a separate MR.
	/*
	if (!TryGetConstantColor(JsonMaterial.EmissiveFactor, MP_EmissiveColor, Material))
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.EmissiveTexture, MP_EmissiveColor, Material, { RgbaMask, RgbMask }))
		{
			if (TryGetBakedMaterialProperty(Builder, JsonMaterial.EmissiveTexture, JsonMaterial.EmissiveFactor, MP_EmissiveColor, Material))
			{
				if (JsonMaterial.EmissiveTexture.Index != INDEX_NONE)
				{
					JsonMaterial.EmissiveFactor = FGLTFJsonColor3::White;	// make sure texture is not multiplied with black
				}
			}
			else
			{
				Builder.AddWarningMessage(TEXT("Failed to export EmissiveColor for material ") + Material->GetName());
			}
		}
		else
		{
			JsonMaterial.EmissiveFactor = FGLTFJsonColor3::White;	// make sure texture is not multiplied with black
		}
	}
	*/

	if (IsPropertyNonDefault(MP_Normal, Material))
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.NormalTexture, MP_Normal, Material, { RgbaMask, RgbMask }))
		{
			if (!TryGetBakedMaterialProperty(Builder, JsonMaterial.NormalTexture, MP_Normal, Material))
			{
				Builder.AddWarningMessage(TEXT("Failed to export Normal for material ") + Material->GetName());
			}
		}
	}

	if (IsPropertyNonDefault(MP_AmbientOcclusion, Material))
	{
		if (!TryGetSourceTexture(Builder, JsonMaterial.OcclusionTexture, MP_AmbientOcclusion, Material, { RMask }))
		{
			if (!TryGetBakedMaterialProperty(Builder, JsonMaterial.OcclusionTexture, MP_AmbientOcclusion, Material))
			{
				Builder.AddWarningMessage(TEXT("Failed to export AmbientOcclusion for material ") + Material->GetName());
			}
		}
	}

	return Builder.AddMaterial(JsonMaterial);
}

bool FGLTFMaterialConverter::TryGetBaseColorAndOpacity(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutPBRParams, const UMaterialInterface* Material) const
{
	const EMaterialProperty OpacityProperty = Material->GetBlendMode() == BLEND_Masked ? MP_OpacityMask : MP_Opacity;

	const bool bIsBaseColorConstant = TryGetConstantColor(OutPBRParams.BaseColorFactor, MP_BaseColor, Material);
	const bool bIsOpacityConstant = TryGetConstantScalar(OutPBRParams.BaseColorFactor.A, OpacityProperty, Material);

	if (bIsBaseColorConstant && bIsOpacityConstant)
	{
		return true;
	}

	// NOTE: since we always bake the properties (for now) when atleast property is non-const, we need
	// to reset the constant factors to their defaults. Otherwise the baked value of a constant property
	// would be scaled with the factor, i.e a double scaling.
	OutPBRParams.BaseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };

	const UTexture2D* BaseColorTexture;
	const UTexture2D* OpacityTexture;
	int32 BaseColorTexCoord;
	int32 OpacityTexCoord;

	const FLinearColor BaseColorMask(1.0f, 1.0f, 1.0f, 0.0f);
	const FLinearColor OpacityMask(0.0f, 0.0f, 0.0f, 1.0f);

	const bool bHasBaseColorSourceTexture = TryGetSourceTexture(BaseColorTexture, BaseColorTexCoord, MP_BaseColor, Material, { BaseColorMask });
	const bool bHasOpacitySourceTexture = TryGetSourceTexture(OpacityTexture, OpacityTexCoord, OpacityProperty, Material, { OpacityMask });

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

	// TODO: should this be the default wrap-mode?
	EGLTFJsonTextureWrap TextureWrapS = EGLTFJsonTextureWrap::Repeat;
	EGLTFJsonTextureWrap TextureWrapT = EGLTFJsonTextureWrap::Repeat;

	// TODO: should this be the default filter?
	EGLTFJsonTextureFilter TextureMinFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;
	EGLTFJsonTextureFilter TextureMagFilter = EGLTFJsonTextureFilter::Linear;

	if (bHasBaseColorSourceTexture && bHasOpacitySourceTexture)
	{
		const bool bAreTexturesCompatible = BaseColorTexCoord == OpacityTexCoord &&
			BaseColorTexture->AddressX == OpacityTexture->AddressX &&
			BaseColorTexture->AddressY == OpacityTexture->AddressY;

		if (!bAreTexturesCompatible)
		{
			// TODO: handle differences in wrapping or uv-coords
			Builder.AddWarningMessage(
				TEXT("BaseColor- and Opacity-textures for material ") +
				Material->GetName() +
				TEXT(" were not able to be combined and will be skipped"));

			return false;
		}

		TexCoord = BaseColorTexCoord;
		TextureSize =
		{
			FMath::Max(BaseColorTexture->GetSizeX(), OpacityTexture->GetSizeX()),
			FMath::Max(BaseColorTexture->GetSizeY(), OpacityTexture->GetSizeY())
		};

		TextureWrapS = FGLTFConverterUtility::ConvertWrap(BaseColorTexture->AddressX);
		TextureWrapT = FGLTFConverterUtility::ConvertWrap(BaseColorTexture->AddressY);

		// TODO: compare min- and mag-filter for BaseColorTexture and OpacityTexture. If they differ,
		// we should choose one or the other and inform the user about the choice made by logging to the console.
		TextureMinFilter = FGLTFConverterUtility::ConvertMinFilter(BaseColorTexture->Filter, BaseColorTexture->LODGroup);
		TextureMagFilter = FGLTFConverterUtility::ConvertMagFilter(BaseColorTexture->Filter, BaseColorTexture->LODGroup);
	}
	else if (bHasBaseColorSourceTexture)
	{
		TexCoord = BaseColorTexCoord;
		TextureSize = { BaseColorTexture->GetSizeX(), BaseColorTexture->GetSizeY() };
		TextureWrapS = FGLTFConverterUtility::ConvertWrap(BaseColorTexture->AddressX);
		TextureWrapT = FGLTFConverterUtility::ConvertWrap(BaseColorTexture->AddressY);
		TextureMinFilter = FGLTFConverterUtility::ConvertMinFilter(BaseColorTexture->Filter, BaseColorTexture->LODGroup);
		TextureMagFilter = FGLTFConverterUtility::ConvertMagFilter(BaseColorTexture->Filter, BaseColorTexture->LODGroup);
	}
	else if (bHasOpacitySourceTexture)
	{
		TexCoord = OpacityTexCoord;
		TextureSize = { OpacityTexture->GetSizeX(), OpacityTexture->GetSizeY() };
		TextureWrapS = FGLTFConverterUtility::ConvertWrap(OpacityTexture->AddressX);
		TextureWrapT = FGLTFConverterUtility::ConvertWrap(OpacityTexture->AddressY);
		TextureMinFilter = FGLTFConverterUtility::ConvertMinFilter(OpacityTexture->Filter, OpacityTexture->LODGroup);
		TextureMagFilter = FGLTFConverterUtility::ConvertMagFilter(OpacityTexture->Filter, OpacityTexture->LODGroup);
	}

	const FGLTFPropertyBakeOutput BaseColorBakeOutput = BakeMaterialProperty(MP_BaseColor, Material, &TextureSize);
	FGLTFPropertyBakeOutput OpacityBakeOutput = BakeMaterialProperty(OpacityProperty, Material, &TextureSize, true);

	// Detect when both baked properties are constants, which means we can avoid exporting a texture
	if (BaseColorBakeOutput.bIsConstant && OpacityBakeOutput.bIsConstant)
	{
		FLinearColor BaseColorFactor(BaseColorBakeOutput.ConstantValue);
		BaseColorFactor.A = OpacityBakeOutput.ConstantValue.A;

		OutPBRParams.BaseColorFactor = FGLTFConverterUtility::ConvertColor(BaseColorFactor);
		return true;
	}

	TextureSize = BaseColorBakeOutput.Size.ComponentMax(OpacityBakeOutput.Size);
	BaseColorTexture = FGLTFMaterialUtility::CreateTransientTexture(BaseColorBakeOutput);
	OpacityTexture = FGLTFMaterialUtility::CreateTransientTexture(OpacityBakeOutput);

	const FString TextureName = Material->GetName() + TEXT("_BaseColor");

	const TArray<FGLTFTextureCombineSource> CombineSources =
	{
		{ OpacityTexture, OpacityMask, SE_BLEND_Opaque },
		{ BaseColorTexture, BaseColorMask }
	};

	const FGLTFJsonTextureIndex TextureIndex = FGLTFMaterialUtility::AddCombinedTexture(
		Builder,
		CombineSources,
		TextureSize,
		TextureName,
		TextureMinFilter,
		TextureMagFilter,
		TextureWrapS,
		TextureWrapT);

	OutPBRParams.BaseColorTexture.TexCoord = TexCoord;
	OutPBRParams.BaseColorTexture.Index = TextureIndex;

	return true;
}

bool FGLTFMaterialConverter::TryGetMetallicAndRoughness(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutPBRParams, const UMaterialInterface* Material) const
{
	const bool bIsMetallicConstant = TryGetConstantScalar(OutPBRParams.MetallicFactor, MP_Metallic, Material);
	const bool bIsRoughnessConstant = TryGetConstantScalar(OutPBRParams.RoughnessFactor, MP_Roughness, Material);

	if (bIsMetallicConstant && bIsRoughnessConstant)
	{
		return true;
	}

	// NOTE: since we always bake the properties (for now) when atleast one property is non-const, we need
	// to reset the constant factors to their defaults. Otherwise the baked value of a constant property
	// would be scaled with the factor, i.e a double scaling.
	OutPBRParams.MetallicFactor = 1.0f;
	OutPBRParams.RoughnessFactor = 1.0f;

	const UTexture2D* MetallicTexture;
	const UTexture2D* RoughnessTexture;
	int32 MetallicTexCoord;
	int32 RoughnessTexCoord;

	const FLinearColor MetallicMask(0.0f, 0.0f, 1.0f, 0.0f);
	const FLinearColor RoughnessMask(0.0f, 1.0f, 0.0f, 0.0f);
	const FLinearColor AlphaMask(0.0f, 0.0f, 0.0f, 1.0f);

	const bool bHasMetallicSourceTexture = TryGetSourceTexture(MetallicTexture, MetallicTexCoord, MP_Metallic, Material, { MetallicMask });
	const bool bHasRoughnessSourceTexture = TryGetSourceTexture(RoughnessTexture, RoughnessTexCoord, MP_Roughness, Material, { RoughnessMask });

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

	// TODO: should this be the default wrap-mode?
	EGLTFJsonTextureWrap TextureWrapS = EGLTFJsonTextureWrap::Repeat;
	EGLTFJsonTextureWrap TextureWrapT = EGLTFJsonTextureWrap::Repeat;

	// TODO: should this be the default filter?
	EGLTFJsonTextureFilter TextureMinFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;
	EGLTFJsonTextureFilter TextureMagFilter = EGLTFJsonTextureFilter::Linear;

	if (bHasMetallicSourceTexture && bHasRoughnessSourceTexture)
	{
		const bool bAreTexturesCompatible = MetallicTexCoord == RoughnessTexCoord &&
			MetallicTexture->AddressX == RoughnessTexture->AddressX &&
			MetallicTexture->AddressY == RoughnessTexture->AddressY;

		if (!bAreTexturesCompatible)
		{
			// TODO: handle differences in wrapping or uv-coords
			Builder.AddWarningMessage(
				TEXT("Metallic- and Roughness-textures for material ") +
				Material->GetName() +
				TEXT(" were not able to be combined and will be skipped"));

			return false;
		}

		TexCoord = MetallicTexCoord;
		TextureSize =
		{
			FMath::Max(MetallicTexture->GetSizeX(), RoughnessTexture->GetSizeX()),
			FMath::Max(MetallicTexture->GetSizeY(), RoughnessTexture->GetSizeY())
		};

		TextureWrapS = FGLTFConverterUtility::ConvertWrap(MetallicTexture->AddressX);
		TextureWrapT = FGLTFConverterUtility::ConvertWrap(MetallicTexture->AddressY);

		// TODO: compare min- and mag-filter for BaseColorTexture and OpacityTexture. If they differ,
		// we should choose one or the other and inform the user about the choice made by logging to the console.
		TextureMinFilter = FGLTFConverterUtility::ConvertMinFilter(MetallicTexture->Filter, MetallicTexture->LODGroup);
		TextureMagFilter = FGLTFConverterUtility::ConvertMagFilter(MetallicTexture->Filter, MetallicTexture->LODGroup);
	}
	else if (bHasMetallicSourceTexture)
	{
		TexCoord = MetallicTexCoord;
		TextureSize = { MetallicTexture->GetSizeX(), MetallicTexture->GetSizeY() };
		TextureWrapS = FGLTFConverterUtility::ConvertWrap(MetallicTexture->AddressX);
		TextureWrapT = FGLTFConverterUtility::ConvertWrap(MetallicTexture->AddressY);
		TextureMinFilter = FGLTFConverterUtility::ConvertMinFilter(MetallicTexture->Filter, MetallicTexture->LODGroup);
		TextureMagFilter = FGLTFConverterUtility::ConvertMagFilter(MetallicTexture->Filter, MetallicTexture->LODGroup);
	}
	else if (bHasRoughnessSourceTexture)
	{
		TexCoord = RoughnessTexCoord;
		TextureSize = { RoughnessTexture->GetSizeX(), RoughnessTexture->GetSizeY() };
		TextureWrapS = FGLTFConverterUtility::ConvertWrap(RoughnessTexture->AddressX);
		TextureWrapT = FGLTFConverterUtility::ConvertWrap(RoughnessTexture->AddressY);
		TextureMinFilter = FGLTFConverterUtility::ConvertMinFilter(RoughnessTexture->Filter, RoughnessTexture->LODGroup);
		TextureMagFilter = FGLTFConverterUtility::ConvertMagFilter(RoughnessTexture->Filter, RoughnessTexture->LODGroup);
	}

	const FGLTFPropertyBakeOutput RoughnessBakeOutput = BakeMaterialProperty(MP_Roughness, Material, &TextureSize);
	const FGLTFPropertyBakeOutput MetallicBakeOutput = BakeMaterialProperty(MP_Metallic, Material, &TextureSize);

	// Detect when both baked properties are constants, which means we can use factors and avoid exporting a texture
	if (RoughnessBakeOutput.bIsConstant && MetallicBakeOutput.bIsConstant)
	{
		OutPBRParams.RoughnessFactor = RoughnessBakeOutput.ConstantValue.R;
		OutPBRParams.MetallicFactor = MetallicBakeOutput.ConstantValue.R;
		return true;
	}

	TextureSize = RoughnessBakeOutput.Size.ComponentMax(MetallicBakeOutput.Size);
	RoughnessTexture = FGLTFMaterialUtility::CreateTransientTexture(RoughnessBakeOutput);
	MetallicTexture = FGLTFMaterialUtility::CreateTransientTexture(MetallicBakeOutput);

	const FString TextureName = Material->GetName() + TEXT("_MetallicRoughness");

	const TArray<FGLTFTextureCombineSource> CombineSources =
	{
		{ MetallicTexture, MetallicMask + AlphaMask, SE_BLEND_Opaque },
		{ RoughnessTexture, RoughnessMask }
	};

	const FGLTFJsonTextureIndex TextureIndex = FGLTFMaterialUtility::AddCombinedTexture(
		Builder,
		CombineSources,
		TextureSize,
		TextureName,
		TextureMinFilter,
		TextureMagFilter,
		TextureWrapS,
		TextureWrapT);

	OutPBRParams.MetallicRoughnessTexture.TexCoord = TexCoord;
	OutPBRParams.MetallicRoughnessTexture.Index = TextureIndex;

	return true;
}

bool FGLTFMaterialConverter::IsPropertyNonDefault(EMaterialProperty Property, const UMaterialInterface* Material) const
{
	const bool bUseMaterialAttributes = Material->GetMaterial()->bUseMaterialAttributes;
	if (bUseMaterialAttributes)
	{
		// TODO: check if attribute property connected, i.e. Material->GetMaterial()->MaterialAttributes.IsConnected(Property)
		return true;
	}

	const FExpressionInput* MaterialInput = FGLTFMaterialUtility::GetInputFromProperty(Material, Property);
	if (MaterialInput == nullptr)
	{
		// TODO: report error
		return false;
	}

	const UMaterialExpression* Expression = MaterialInput->Expression;
	if (Expression == nullptr)
	{
		return false;
	}

	return true;
}

bool FGLTFMaterialConverter::TryGetConstantColor(FGLTFJsonColor3& OutValue, EMaterialProperty Property, const UMaterialInterface* Material) const
{
	FLinearColor Value;
	if (TryGetConstantColor(Value, Property, Material))
	{
		OutValue = FGLTFConverterUtility::ConvertColor(Value);
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetConstantColor(FGLTFJsonColor4& OutValue, EMaterialProperty Property, const UMaterialInterface* Material) const
{
	FLinearColor Value;
	if (TryGetConstantColor(Value, Property, Material))
	{
		OutValue = FGLTFConverterUtility::ConvertColor(Value);
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetConstantColor(FLinearColor& OutValue, EMaterialProperty Property, const UMaterialInterface* Material) const
{
	// TODO: handle emissive color-values above 1.0

	const bool bUseMaterialAttributes = Material->GetMaterial()->bUseMaterialAttributes;
	if (bUseMaterialAttributes)
	{
		// TODO: check if attribute property connected, i.e. Material->GetMaterial()->MaterialAttributes.IsConnected(Property)
		return false;
	}

	const FMaterialInput<FColor>* MaterialInput = FGLTFMaterialUtility::GetInputFromProperty<FColor>(Material, Property);
	if (MaterialInput == nullptr)
	{
		// TODO: report error
		return false;
	}

	if (MaterialInput->UseConstant)
	{
		OutValue = { MaterialInput->Constant };
		return true;
	}

	const UMaterialExpression* Expression = MaterialInput->Expression;
	if (Expression == nullptr)
	{
		OutValue = FLinearColor(FGLTFMaterialUtility::GetPropertyDefaultValue(Property));
		return true;
	}

	if (const UMaterialExpressionVectorParameter* VectorParameter = ExactCast<UMaterialExpressionVectorParameter>(Expression))
	{
		FLinearColor Value = VectorParameter->DefaultValue;

		const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material);
		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(VectorParameter->GetParameterName());
			if (!MaterialInstance->GetVectorParameterValue(ParameterInfo, Value))
			{
				// TODO: how to handle this?
			}
		}

		const uint32 MaskComponentCount = FGLTFMaterialUtility::GetMaskComponentCount(*MaterialInput);

		if (MaskComponentCount > 0)
		{
			const FLinearColor Mask = FGLTFMaterialUtility::GetMask(*MaterialInput);

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

		const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material);
		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(ScalarParameter->GetParameterName());
			if (!MaterialInstance->GetScalarParameterValue(ParameterInfo, Value))
			{
				// TODO: how to handle this?
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

bool FGLTFMaterialConverter::TryGetConstantScalar(float& OutValue, EMaterialProperty Property, const UMaterialInterface* Material) const
{
	const bool bUseMaterialAttributes = Material->GetMaterial()->bUseMaterialAttributes;
	if (bUseMaterialAttributes)
	{
		// TODO: check if attribute property connected, i.e. Material->GetMaterial()->MaterialAttributes.IsConnected(Property)
		return false;
	}

	const FMaterialInput<float>* MaterialInput = FGLTFMaterialUtility::GetInputFromProperty<float>(Material, Property);
	if (MaterialInput == nullptr)
	{
		// TODO: report error
		return false;
	}

	if (MaterialInput->UseConstant)
	{
		OutValue = MaterialInput->Constant;
		return true;
	}

	const UMaterialExpression* Expression = MaterialInput->Expression;
	if (Expression == nullptr)
	{
		OutValue = FGLTFMaterialUtility::GetPropertyDefaultValue(Property).X;
		return true;
	}

	if (const UMaterialExpressionVectorParameter* VectorParameter = ExactCast<UMaterialExpressionVectorParameter>(Expression))
	{
		FLinearColor Value = VectorParameter->DefaultValue;

		const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material);
		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(VectorParameter->GetParameterName());
			if (!MaterialInstance->GetVectorParameterValue(ParameterInfo, Value))
			{
				// TODO: how to handle this?
			}
		}

		const uint32 MaskComponentCount = FGLTFMaterialUtility::GetMaskComponentCount(*MaterialInput);

		if (MaskComponentCount > 0)
		{
			const FLinearColor Mask = FGLTFMaterialUtility::GetMask(*MaterialInput);
			Value *= Mask;
		}

		// TODO: is this a correct assumption, that the max component should be used as value?
		OutValue = Value.GetMax();
		return true;
	}

	if (const UMaterialExpressionScalarParameter* ScalarParameter = ExactCast<UMaterialExpressionScalarParameter>(Expression))
	{
		float Value = ScalarParameter->DefaultValue;

		const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material);
		if (MaterialInstance != nullptr)
		{
			const FHashedMaterialParameterInfo ParameterInfo(ScalarParameter->GetParameterName());
			if (!MaterialInstance->GetScalarParameterValue(ParameterInfo, Value))
			{
				// TODO: how to handle this?
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

bool FGLTFMaterialConverter::TryGetSourceTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, EMaterialProperty Property, const UMaterialInterface* Material, const TArray<FLinearColor>& AllowedMasks) const
{
	const UTexture2D* Texture;
	int32 TexCoord;

	if (TryGetSourceTexture(Texture, TexCoord, Property, Material, AllowedMasks))
	{
		OutTexInfo.Index = Builder.GetOrAddTexture(Texture);
		OutTexInfo.TexCoord = TexCoord;
		return true;
	}

	return false;
}

bool FGLTFMaterialConverter::TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, EMaterialProperty Property, const UMaterialInterface* Material, const TArray<FLinearColor>& AllowedMasks) const
{
	const FExpressionInput* MaterialInput = FGLTFMaterialUtility::GetInputFromProperty(Material, Property);
	if (MaterialInput == nullptr)
	{
		// TODO: report error
		return false;
	}

	const UMaterialExpression* Expression = MaterialInput->Expression;
	if (Expression == nullptr)
	{
		return false;
	}

	const FLinearColor InputMask = FGLTFMaterialUtility::GetMask(*MaterialInput);
	if (AllowedMasks.Num() > 0 && !AllowedMasks.Contains(InputMask))
	{
		return false;
	}

	if (const UMaterialExpressionTextureSampleParameter2D* TextureParameter = ExactCast<UMaterialExpressionTextureSampleParameter2D>(Expression))
	{
		const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material);
		if (MaterialInstance != nullptr)
		{
			UTexture* ParameterValue;

			const FHashedMaterialParameterInfo ParameterInfo(TextureParameter->GetParameterName());
			if (!MaterialInstance->GetTextureParameterValue(ParameterInfo, ParameterValue))
			{
				// TODO: how to handle this?
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

bool FGLTFMaterialConverter::TryGetBakedMaterialProperty(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor3& OutConstant, EMaterialProperty Property, const UMaterialInterface* Material) const
{
	const FGLTFPropertyBakeOutput PropertyBakeOutput = BakeMaterialProperty(Property, Material);

	if (PropertyBakeOutput.bIsConstant)
	{
		OutConstant = FGLTFConverterUtility::ConvertColor(PropertyBakeOutput.ConstantValue);
		return true;
	}
	else
	{
		return StoreBakedPropertyTexture(Builder, OutTexInfo, PropertyBakeOutput, Property, Material);
	}
}

bool FGLTFMaterialConverter::TryGetBakedMaterialProperty(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor4& OutConstant, EMaterialProperty Property, const UMaterialInterface* Material) const
{
	const FGLTFPropertyBakeOutput PropertyBakeOutput = BakeMaterialProperty(Property, Material);

	if (PropertyBakeOutput.bIsConstant)
	{
		OutConstant = FGLTFConverterUtility::ConvertColor(PropertyBakeOutput.ConstantValue);
		return true;
	}
	else
	{
		return StoreBakedPropertyTexture(Builder, OutTexInfo, PropertyBakeOutput, Property, Material);
	}
}

bool FGLTFMaterialConverter::TryGetBakedMaterialProperty(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, EMaterialProperty Property, const UMaterialInterface* Material) const
{
	const FGLTFPropertyBakeOutput PropertyBakeOutput = BakeMaterialProperty(Property, Material);

	if (!PropertyBakeOutput.bIsConstant)
	{
		return StoreBakedPropertyTexture(Builder, OutTexInfo, PropertyBakeOutput, Property, Material);
	}
	else
	{
		// NOTE: since this function is meant to bake to a texture, we assume that the property
		// that was passed into the function is using a non-constant expression.
		// We therefore treat a constant result when baking as a failure.

		// NOTE: in some cases a constant baking result is returned for a property that is non-constant.
		// This happens (for example) when baking AmbientOcclusion for a translucent material,
		// even though the same material when set to opaque will properly bake AmbientOcclusion to a texture.
		// It also happens when baking Normal in some (not yet identified) circumstances.
		// For now, these incorrect bakes are discarded.

		// TODO: investigate why non-constant properties are sometimes baked to a constant expression (see note above)
		return false;
	}
}

FGLTFPropertyBakeOutput FGLTFMaterialConverter::BakeMaterialProperty(EMaterialProperty Property, const UMaterialInterface* Material, const FIntPoint* PreferredTextureSize, bool bCopyAlphaFromRedChannel) const
{
	// TODO: make default baking-resolution configurable
	const FIntPoint DefaultTextureSize(512, 512);
	const FIntPoint TextureSize = PreferredTextureSize != nullptr ? *PreferredTextureSize : DefaultTextureSize;

	// TODO: handle cases where PropertyBakeOutput.EmissiveScale is not 1.0 (when baking EmissiveColor)
	// TODO: add support for calculating the ideal resolution to use for baking based on connected (texture) nodes

	const FGLTFPropertyBakeOutput PropertyBakeOutput = FGLTFMaterialUtility::BakeMaterialProperty(
		TextureSize,
		Property,
		Material,
		bCopyAlphaFromRedChannel);

	return PropertyBakeOutput;
}

bool FGLTFMaterialConverter::StoreBakedPropertyTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, const FGLTFPropertyBakeOutput& PropertyBakeOutput, EMaterialProperty Property, const UMaterialInterface* Material) const
{
	TCHAR* PropertyName;

	switch (Property)
	{
		case MP_BaseColor: PropertyName = TEXT("BaseColor"); break;
		case MP_Normal: PropertyName = TEXT("Normal"); break;
		case MP_EmissiveColor: PropertyName = TEXT("Emissive"); break;
		case MP_AmbientOcclusion: PropertyName = TEXT("Occlusion"); break;
		default:
			// TODO: support for more properties
			return false;
	}

	const FString TextureName = Material->GetName() + TEXT("_") + PropertyName;

	// TODO: add support for detecting the correct tex-coord for this property based on connected nodes
	const uint32 TexCoord = 0;

	// TODO: should this be the default wrap-mode?
	const EGLTFJsonTextureWrap TextureWrapS = EGLTFJsonTextureWrap::Repeat;
	const EGLTFJsonTextureWrap TextureWrapT = EGLTFJsonTextureWrap::Repeat;

	// TODO: should this be the default filter?
	const EGLTFJsonTextureFilter TextureMinFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;
	const EGLTFJsonTextureFilter TextureMagFilter = EGLTFJsonTextureFilter::Linear;

	const FGLTFJsonTextureIndex TextureIndex = FGLTFMaterialUtility::AddTexture(
		Builder,
		PropertyBakeOutput.Pixels,
		PropertyBakeOutput.Size,
		TextureName,
		PropertyBakeOutput.PixelFormat,
		TextureMinFilter,
		TextureMagFilter,
		TextureWrapS,
		TextureWrapT);

	OutTexInfo.TexCoord = TexCoord;
	OutTexInfo.Index = TextureIndex;

	return true;
}
