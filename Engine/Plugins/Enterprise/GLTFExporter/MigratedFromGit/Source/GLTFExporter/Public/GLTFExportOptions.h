// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GLTFExportOptions.generated.h"

UENUM(BlueprintType)
enum class EGLTFExporterMaterialBakeSize : uint8
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
enum class EGLTFExporterTextureHDREncoding : uint8
{
	None,
	RGBM,
	RGBE,
	HDR
};

UENUM(BlueprintType)
enum class EGLTFExporterLightMobility : uint8
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	bool bBakeMaterialInputsUsingMeshData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Material)
	EGLTFExporterMaterialBakeSize DefaultMaterialBakeSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	bool bExportVertexColors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	bool bQuantizeVertexNormals;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	bool bQuantizeVertexTangents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Mesh)
	int32 DefaultLevelOfDetail;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportLevelSequences;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportVertexSkinWeights;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportAnimationSequences;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bRetargetBoneTransforms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Animation)
	bool bExportPlaybackSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	bool bExportTextureTransforms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	bool bExportLightmaps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Texture)
	EGLTFExporterTextureHDREncoding TextureHDREncoding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene, meta =
		(DisplayName = "Export Uniform Scale",
		 ToolTip = "Scale factor used for exporting assets, by default: 0.01, for conversion from centimeters(Unreal default) to meters(glTF)."))
	float ExportScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportHiddenInGame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	EGLTFExporterLightMobility ExportLights;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportCameras;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
	bool bExportOrbitalCameras;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = Scene)
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

	/* Set all the each property to the CDO value */
	void ResetToDefault();

	/* Save the each property to a local ini to retrieve the value the next time we call function LoadOptions() */
	void SaveOptions();

	/* Load the each property from a local ini which the value was store by the function SaveOptions() */
	void LoadOptions();

	virtual bool CanEditChange(const FProperty* InProperty) const override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	FIntPoint GetDefaultMaterialBakeSize() const;

	bool ShouldExportLight(EComponentMobility::Type LightMobility) const;
};
