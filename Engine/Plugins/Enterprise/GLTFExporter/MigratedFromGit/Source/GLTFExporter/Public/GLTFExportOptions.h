// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GLTFExportOptions.generated.h"

UENUM(BlueprintType)
enum class EGLTFMaterialBakeSizePOT : uint8
{
	POT_1 UMETA(DisplayName = "1 x 1"),
	POT_2 UMETA(DisplayName = "2 x 2"),
	POT_4 UMETA(DisplayName = "4 x 4"),
	POT_8 UMETA(DisplayName = "8 x 8"),
	POT_16 UMETA(DisplayName = "16 x 16"),
	POT_32 UMETA(DisplayName = "32 x 32"),
	POT_64 UMETA(DisplayName = "64 x 64"),
	POT_128 UMETA(DisplayName = "128 x 128"),
	POT_256 UMETA(DisplayName = "256 x 256"),
	POT_512 UMETA(DisplayName = "512 x 512"),
	POT_1024 UMETA(DisplayName = "1024 x 1024"),
	POT_2048 UMETA(DisplayName = "2048 x 2048"),
	POT_4096 UMETA(DisplayName = "4096 x 4096"),
	POT_8192 UMETA(DisplayName = "8192 x 8192")
};

UENUM(BlueprintType)
enum class EGLTFTextureCompression : uint8
{
	PNG,
    JPEG UMETA(DisplayName = "JPEG (if no alpha)")
};

UENUM(BlueprintType, Meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EGLTFTextureGroupFlags : uint8
{
	None = 0 UMETA(Hidden),

	HDR = 1 << 0,
	Normalmaps = 1 << 1,
    Lightmaps = 1 << 2,

	All = HDR | Normalmaps | Lightmaps UMETA(Hidden)
};
ENUM_CLASS_FLAGS(EGLTFTextureGroupFlags);

UENUM(BlueprintType)
enum class EGLTFTextureHDREncoding : uint8
{
	None,
	RGBM
};

UENUM(BlueprintType)
enum class EGLTFExportLightMobility : uint8
{
	None,
	MovableOnly,
	MovableAndStationary,
	All
};

UCLASS(Config=EditorPerProjectUserSettings, HideCategories=(DebugProperty))
class GLTFEXPORTER_API UGLTFExportOptions : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	bool bExportUnlitMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	bool bExportClearCoatMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	bool bBakeMaterialInputs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material, Meta = (EditCondition = "bBakeMaterialInputs"))
	bool bMaterialBakeUsingMeshData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material, Meta = (EditCondition = "bBakeMaterialInputs"))
	EGLTFMaterialBakeSizePOT DefaultMaterialBakeSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	bool bExportVertexColors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	bool bExportMeshQuantization;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh, Meta = (ClampMin = "0"))
	int32 DefaultLevelOfDetail;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportLevelSequences;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportVertexSkinWeights;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation, Meta = (EditCondition = "bExportVertexSkinWeights"))
	bool bExportAnimationSequences;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation, Meta = (EditCondition = "bExportVertexSkinWeights && bExportAnimationSequences"))
	bool bRetargetBoneTransforms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportPlaybackSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	EGLTFTextureCompression TextureCompression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture, Meta = (ClampMin = "0", ClampMax = "100", EditCondition = "TextureCompression != EGLTFExporterTextureCompression::PNG"))
	int32 TextureCompressionQuality;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture, Meta = (Bitmask, BitmaskEnum = EGLTFTextureGroupFlags, EditCondition = "TextureCompression != EGLTFExporterTextureCompression::PNG"))
	int32 LosslessCompressTextures;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	bool bExportTextureTransforms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	bool bExportLightmaps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture, Meta = (DisplayName = "Texture HDR Encoding"))
	EGLTFTextureHDREncoding TextureHDREncoding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene, Meta =
		(DisplayName = "Export Uniform Scale",
		 ToolTip = "Scale factor used for exporting assets, by default: 0.01, for conversion from centimeters(Unreal default) to meters(glTF)."))
	float ExportScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportHiddenInGame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	EGLTFExportLightMobility ExportLights;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportCameras;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene, Meta = (EditCondition = "bExportCameras"))
	bool bExportCameraControls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene, Meta = (DisplayName = "Export HDRI Backdrops"))
	bool bExportHDRIBackdrops;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportSkySpheres;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportVariantSets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportAnimationHotspots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Exporter)
	bool bBundleWebViewer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Exporter)
	bool bExportPreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Exporter)
	bool bAllExtensionsRequired;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Exporter)
	bool bShowFilesWhenDone;

	void ResetToDefault();
};
