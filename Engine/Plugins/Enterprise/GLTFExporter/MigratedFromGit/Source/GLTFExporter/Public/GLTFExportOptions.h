// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UserData/GLTFMaterialUserData.h"
#include "GLTFExportOptions.generated.h"

UENUM(BlueprintType)
enum class EGLTFTextureCompression : uint8
{
	PNG,
    JPEG UMETA(DisplayName = "JPEG (if no alpha)")
};

UENUM(BlueprintType, Meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EGLTFTextureType : uint8
{
	None = 0 UMETA(Hidden),

	HDR = 1 << 0,
	Normalmaps = 1 << 1,
    Lightmaps = 1 << 2,

	All = HDR | Normalmaps | Lightmaps UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EGLTFTextureType);

UENUM(BlueprintType)
enum class EGLTFTextureHDREncoding : uint8
{
	None,
	RGBM
};

UENUM(BlueprintType, Meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EGLTFSceneMobility : uint8
{
	None = 0 UMETA(Hidden),

	Static = 1 << 0,
	Stationary = 1 << 1,
	Movable = 1 << 2,

	All = Static | Stationary | Movable UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EGLTFSceneMobility);

UENUM(BlueprintType)
enum class EGLTFMaterialVariantMode : uint8
{
	None,
    Simple,
    UseMeshData,
};

UCLASS(Config=EditorPerProjectUserSettings, HideCategories=(DebugProperty))
class GLTFEXPORTER_API UGLTFExportOptions : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Scale factor used for exporting assets (0.01 by default) for conversion from centimeters (Unreal default) to meters (glTF). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = General)
	float ExportUniformScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = General)
	bool bExportPreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = General)
	bool bBundleWebViewer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = General)
	bool bShowFilesWhenDone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	bool bExportUnlitMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	bool bExportClearCoatMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	bool bExportExtraBlendModes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	EGLTFMaterialBakeMode BakeMaterialInputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material, Meta = (EditCondition = "BakeMaterialInputs != EGLTFMaterialBakeMode::None"))
	EGLTFMaterialBakeSizePOT DefaultMaterialBakeSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material, Meta = (EditCondition = "BakeMaterialInputs != EGLTFMaterialBakeMode::None", ValidEnumValues="TF_Nearest, TF_Bilinear, TF_Trilinear"))
	TEnumAsByte<TextureFilter> DefaultMaterialBakeFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material, Meta = (EditCondition = "BakeMaterialInputs != EGLTFMaterialBakeMode::None"))
	TEnumAsByte<TextureAddress> DefaultMaterialBakeTiling;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	TMap<EGLTFMaterialPropertyGroup, FGLTFOverrideMaterialBakeSettings> OverrideBakeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh, Meta = (ClampMin = "0"))
	int32 DefaultLevelOfDetail;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	bool bExportVertexColors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	bool bExportVertexSkinWeights;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	bool bExportMeshQuantization;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportLevelSequences;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation, Meta = (EditCondition = "bExportVertexSkinWeights"))
	bool bExportAnimationSequences;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation, Meta = (EditCondition = "bExportVertexSkinWeights && bExportAnimationSequences"))
	bool bRetargetBoneTransforms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportPlaybackSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	EGLTFTextureCompression TextureCompression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture, Meta = (ClampMin = "0", ClampMax = "100", EditCondition = "TextureCompression != EGLTFTextureCompression::PNG"))
	int32 TextureCompressionQuality;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture, Meta = (Bitmask, BitmaskEnum = EGLTFTextureType, EditCondition = "TextureCompression != EGLTFTextureCompression::PNG"))
	int32 NoLossyCompressionFor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	bool bExportTextureTransforms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	bool bExportLightmaps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture, Meta = (DisplayName = "Texture HDR Encoding"))
	EGLTFTextureHDREncoding TextureHDREncoding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportHiddenInGame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene, Meta = (Bitmask, BitmaskEnum = EGLTFSceneMobility))
	int32 ExportLights;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportCameras;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene, Meta = (EditCondition = "bExportCameras"))
	bool bExportCameraControls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportAnimationHotspots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene, Meta = (DisplayName = "Export HDRI Backdrops"))
	bool bExportHDRIBackdrops;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportSkySpheres;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportVariantSets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = VariantSets, Meta = (EditCondition = "bExportVariantSets"))
	EGLTFMaterialVariantMode ExportMaterialVariants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = VariantSets, Meta = (EditCondition = "bExportVariantSets"))
	bool bExportMeshVariants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = VariantSets, Meta = (EditCondition = "bExportVariantSets"))
	bool bExportVisibilityVariants;

	void ResetToDefault();
};
