// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Options/GLTFExportOptions.h"
#include "GLTFPrebakeOptions.generated.h"

UCLASS(BlueprintType, Config=EditorPerProjectUserSettings, HideCategories=(DebugProperty))
class GLTFEXPORTER_API UGLTFPrebakeOptions : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Default size of the baked out texture (containing the material input). Can be overridden by material- and input-specific bake settings, see GLTFMaterialExportOptions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	EGLTFMaterialBakeSizePOT DefaultMaterialBakeSize;

	/** Default filtering mode used when sampling the baked out texture. Can be overridden by material- and input-specific bake settings, see GLTFMaterialExportOptions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material, Meta = (ValidEnumValues="TF_Nearest, TF_Bilinear, TF_Trilinear"))
	TEnumAsByte<TextureFilter> DefaultMaterialBakeFilter;

	/** Default addressing mode used when sampling the baked out texture. Can be overridden by material- and input-specific bake settings, see GLTFMaterialExportOptions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	TEnumAsByte<TextureAddress> DefaultMaterialBakeTiling;

	/** Input-specific default bake settings that override the general defaults above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	TMap<EGLTFMaterialPropertyGroup, FGLTFOverrideMaterialBakeSettings> DefaultInputBakeSettings;

	UFUNCTION(BlueprintCallable, Category = General)
	void ResetToDefault();
};
