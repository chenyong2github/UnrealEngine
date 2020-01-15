// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EngineTypes.h"
#include "MaterialMerging.generated.h"

UENUM()
enum ETextureSizingType
{
	TextureSizingType_UseSingleTextureSize UMETA(DisplayName = "Use TextureSize for all material properties"),
	TextureSizingType_UseAutomaticBiasedSizes UMETA(DisplayName = "Use automatically biased texture sizes based on TextureSize"),
	TextureSizingType_UseManualOverrideTextureSize UMETA(DisplayName = "Use per property manually overriden texture sizes"),
	TextureSizingType_UseSimplygonAutomaticSizing UMETA(DisplayName = "Use Simplygon's automatic texture sizing"),
	TextureSizingType_MAX,
};

UENUM()
enum EMaterialMergeType
{
	MaterialMergeType_Default,
	MaterialMergeType_Simplygon
};

USTRUCT(Blueprintable)
struct FMaterialProxySettings
{
	GENERATED_USTRUCT_BODY()

	// Size of generated BaseColor map
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere, meta =(ClampMin = "1", UIMin = "1"))
	FIntPoint TextureSize;
	
	// Gutter space to take into account 
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta=(DisplayAfter="TextureSizingType"))
	float GutterSpace;

	// Constant value to use for the Metallic property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere, meta = (DisplayAfter="bMetallicMap", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1", editcondition = "!bMetallicMap"))
	float MetallicConstant;

	// Constant value to use for the Roughness property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere, meta = (DisplayAfter="bRoughnessMap", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1", editcondition = "!bRoughnessMap"))
	float RoughnessConstant;

	// Constant value to use for the Specular property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere, meta = (DisplayAfter="bSpecularMap", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1", editcondition = "!bSpecularMap"))
	float SpecularConstant;

	// Constant value to use for the Opacity property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere, meta = (DisplayAfter="bOpacityMap", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1", editcondition = "!bOpacityMap"))
	float OpacityConstant;

	// Constant value to use for the Opacity mask property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere, meta = (DisplayAfter="bOpacityMaskMap", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1", editcondition = "!bOpacityMaskMap"))
	float OpacityMaskConstant;

	// Constant value to use for the Ambient Occlusion property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere, meta = (DisplayAfter="bAmbientOcclusionMap", ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1", editcondition = "!bAmbientOcclusionMap"))
	float AmbientOcclusionConstant;

	// Method that should be used to generate the sizes of the output textures
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	TEnumAsByte<ETextureSizingType> TextureSizingType;

	UPROPERTY()
	TEnumAsByte<EMaterialMergeType> MaterialMergeType;

	// Target blend mode for the generated material
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta=(DisplayAfter="AmbientOcclusionTextureSize"))
	TEnumAsByte<EBlendMode> BlendMode;

	// Whether or not to allow the generated material can be two-sided
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere, meta = (DisplayAfter = "BlendMode"))
	uint8 bAllowTwoSidedMaterial : 1;

	// Whether to generate a texture for the Normal property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	uint8 bNormalMap:1;

	// Whether to generate a texture for the Metallic property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	uint8 bMetallicMap:1;

	// Whether to generate a texture for the Roughness property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	uint8 bRoughnessMap:1;

	// Whether to generate a texture for the Specular property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	uint8 bSpecularMap:1;

	// Whether to generate a texture for the Emissive property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	uint8 bEmissiveMap:1;

	// Whether to generate a texture for the Opacity property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	uint8 bOpacityMap:1;

	// Whether to generate a texture for the Opacity Mask property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	uint8 bOpacityMaskMap:1;

	// Whether to generate a texture for the Ambient Occlusion property
	UPROPERTY(Category = Material, BlueprintReadWrite, EditAnywhere)
	uint8 bAmbientOcclusionMap:1;

	// Override Diffuse texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint DiffuseTextureSize;

	// Override Normal texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint NormalTextureSize;

	// Override Metallic texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint MetallicTextureSize;

	// Override Roughness texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint RoughnessTextureSize;

	// Override Specular texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint SpecularTextureSize;

	// Override Emissive texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint EmissiveTextureSize;

	// Override Opacity texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint OpacityTextureSize;
	
	// Override Opacity Mask texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint OpacityMaskTextureSize;

	// Override Ambient Occlusion texture size
	UPROPERTY(Category = Material, BlueprintReadWrite, AdvancedDisplay, EditAnywhere, meta = (ClampMin = "1", UIMin = "1"))
	FIntPoint AmbientOcclusionTextureSize;

	FMaterialProxySettings()
		: TextureSize(1024, 1024)
		, GutterSpace(4.0f)
		, MetallicConstant(0.0f)
		, RoughnessConstant(0.5f)
		, SpecularConstant(0.5f)
		, OpacityConstant(1.0f)
		, OpacityMaskConstant(1.0f)
		, AmbientOcclusionConstant(1.0f)
		, TextureSizingType(TextureSizingType_UseSingleTextureSize)
		, MaterialMergeType(EMaterialMergeType::MaterialMergeType_Default)
		, BlendMode(BLEND_Opaque)
		, bAllowTwoSidedMaterial(true)
		, bNormalMap(true)
		, bMetallicMap(false)
		, bRoughnessMap(false)
		, bSpecularMap(false)
		, bEmissiveMap(false)
		, bOpacityMap(false)
		, bOpacityMaskMap(false)
		, bAmbientOcclusionMap(false)
		, DiffuseTextureSize(1024, 1024)
		, NormalTextureSize(1024, 1024)
		, MetallicTextureSize(1024, 1024)
		, RoughnessTextureSize(1024, 1024)
		, SpecularTextureSize(1024, 1024)
		, EmissiveTextureSize(1024, 1024)
		, OpacityTextureSize(1024, 1024)
		, OpacityMaskTextureSize(1024, 1024)
		, AmbientOcclusionTextureSize(1024, 1024)
	{
	}

	bool operator == (const FMaterialProxySettings& Other) const
	{
		return TextureSize == Other.TextureSize
			&& TextureSizingType == Other.TextureSizingType
			&& GutterSpace == Other.GutterSpace
			&& bNormalMap == Other.bNormalMap
			&& MetallicConstant == Other.MetallicConstant
			&& bMetallicMap == Other.bMetallicMap
			&& RoughnessConstant == Other.RoughnessConstant
			&& bRoughnessMap == Other.bRoughnessMap
			&& SpecularConstant == Other.SpecularConstant
			&& bSpecularMap == Other.bSpecularMap
			&& bEmissiveMap == Other.bEmissiveMap
			&& bOpacityMap == Other.bOpacityMap
			&& bOpacityMaskMap == Other.bOpacityMaskMap
			&& bAmbientOcclusionMap == Other.bAmbientOcclusionMap
			&& AmbientOcclusionConstant == Other.AmbientOcclusionConstant
			&& DiffuseTextureSize == Other.DiffuseTextureSize
			&& NormalTextureSize == Other.NormalTextureSize
			&& MetallicTextureSize == Other.MetallicTextureSize
			&& RoughnessTextureSize == Other.RoughnessTextureSize
			&& EmissiveTextureSize == Other.EmissiveTextureSize
			&& OpacityTextureSize == Other.OpacityTextureSize
			&& OpacityMaskTextureSize == Other.OpacityMaskTextureSize
			&& AmbientOcclusionTextureSize == Other.AmbientOcclusionTextureSize;
	}
};
