// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GLTFExportOptions.generated.h"

UENUM(BlueprintType)
enum class EGLTFExporterNormalizeUVCoordinates : uint8
{
	Always,
    Auto,
	Never,
};

UENUM(BlueprintType)
enum class EGLTFExporterTextureFormat : uint8
{
	PNG,
	JPEG
};

UENUM(BlueprintType)
enum class EGLTFExporterTextureHDREncoding : uint8
{
	None,
	HDR,
	RGBM,
	RGBD,
	RGBE
};

UENUM(BlueprintType)
enum class EGLTFExporterTextureSize : uint8
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

UCLASS(config = EditorPerProjectUserSettings, HideCategories = (DebugProperty))
class GLTFEXPORTER_API UGLTFExportOptions : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Exporter)
	uint32 bBundleWebViewer : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, category = Exporter)
	uint32 bExportPreviewMesh : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Exporter)
	uint32 bAllExtensionsRequired : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, category = Mesh)
	uint32 bExportVertexColors : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Mesh)
	uint32 bTangentDataQuantization : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Mesh)
    EGLTFExporterNormalizeUVCoordinates bNormalizeUVCoordinates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, category = Mesh)
	int32 DefaultLevelOfDetail;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, category = Animation)
	uint32 bMapSkeletalMotionToRoot : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Material)
	uint32 bExportUnlitMaterials : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Material)
	uint32 bExportClearCoatMaterials : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, category = Material)
	uint32 bBakeMaterialInputs : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, category = Material)
	EGLTFExporterTextureSize BakedMaterialInputSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Texture)
	EGLTFExporterTextureFormat TextureFormat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Texture)
	EGLTFExporterTextureHDREncoding TextureHDREncoding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Texture)
	uint32 bExportLightmaps : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Scene, meta =
		(DisplayName = "Export Uniform Scale",
		 ToolTip = "Scale factor used for exporting assets, by default: 0.01, for conversion from centimeters(Unreal default) to meters(glTF)."))
	float ExportScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Scene)
	uint32 bExportLights : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Scene)
	uint32 bExportCameras : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Scene)
	uint32 bExportReflectionCaptures : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Scene)
	uint32 bExportHDRIBackdrops : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Scene)
	uint32 bExportVariantSets : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Scene)
	uint32 bExportInteractionHotspots : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = Scene)
    uint32 bExportOrbitalCameras : 1;

	/* Set all the each property to the CDO value */
	void ResetToDefault();

	/* Save the each property to a local ini to retrieve the value the next time we call function LoadOptions() */
	void SaveOptions();

	/* Load the each property from a local ini which the value was store by the function SaveOptions() */
	void LoadOptions();

	/**
	* Load the export option from the last save state and show the dialog if bShowOptionDialog is true.
	* FullPath is the export file path we display it in the dialog
	* If user cancel the dialog, the bOutOperationCanceled will be true
	* bOutExportAll will be true if the user want to use the same option for all other asset he want to export
	*
	* The function is saving the dialog state in a user ini file and reload it from there. It is not changing the CDO.
	*/
	void FillOptions(bool bBatchMode, bool bShowOptionDialog, const FString& FullPath, bool& bOutOperationCanceled, bool& bOutExportAll);
};
