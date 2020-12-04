// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/StaticMesh.h"

#include "DynamicMesh3.h"
#include "Image/ImageBuilder.h"

#include "Graphs/GenerateMeshLODGraph.h"

#include "GenerateStaticMeshLODProcess.generated.h"


class UTexture2D;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;


UENUM()
enum class EGenerateStaticMeshLODBakeResolution
{
	Resolution16 = 16 UMETA(DisplayName = "16 x 16"),
	Resolution32 = 32 UMETA(DisplayName = "32 x 32"),
	Resolution64 = 64 UMETA(DisplayName = "64 x 64"),
	Resolution128 = 128 UMETA(DisplayName = "128 x 128"),
	Resolution256 = 256 UMETA(DisplayName = "256 x 256"),
	Resolution512 = 512 UMETA(DisplayName = "512 x 512"),
	Resolution1024 = 1024 UMETA(DisplayName = "1024 x 1024"),
	Resolution2048 = 2048 UMETA(DisplayName = "2048 x 2048"),
	Resolution4096 = 4096 UMETA(DisplayName = "4096 x 4096"),
	Resolution8192 = 8192 UMETA(DisplayName = "8192 x 8192")
};


USTRUCT()
struct FGenerateStaticMeshLODProcessSettings
{
	GENERATED_BODY()

	// Solidify settings

	UPROPERTY(EditAnywhere, Category = Solidify, meta = (DisplayName="Voxel Resolution"))
	int SolidifyVoxelResolution = 64;

	UPROPERTY(EditAnywhere, Category = Solidify, AdvancedDisplay)
	float WindingThreshold = 0.5f;


	// Morphology settings

	//UPROPERTY(EditAnywhere, Category = Morphology, meta = (DisplayName = "Voxel Resolution"))
	//int MorphologyVoxelResolution = 64;

	UPROPERTY(EditAnywhere, Category = Morphology, meta = (DisplayName = "Closure Distance"))
	float ClosureDistance = 1.0f;


	// Simplify settings

	UPROPERTY(EditAnywhere, Category = Simplify, meta = (DisplayName = "Simplify Tri Count"))
	int SimplifyTriangleCount = 500;


	// UV settings

	UPROPERTY(EditAnywhere, Category = AutoUV, meta = (DisplayName = "AutoUV Charts", UIMin = 0, UIMax = 1000))
	int NumAutoUVCharts = 20;


	// Bake Settings

	UPROPERTY(EditAnywhere, Category = Baking , meta = (DisplayName = "Bake Image Res"))
	EGenerateStaticMeshLODBakeResolution BakeResolution = EGenerateStaticMeshLODBakeResolution::Resolution512;

	UPROPERTY(EditAnywhere, Category = Baking, meta = (DisplayName = "Bake Thickness"))
	float BakeThickness = 5.0f;



	// Convex Hull Settings

	UPROPERTY(EditAnywhere, Category = ConvexCollision, meta = (DisplayName = "Convex Tri Count"))
	int ConvexTriangleCount = 50;
};



class FGenerateStaticMeshLODProcess
{
public:

	bool Initialize(UStaticMesh* SourceMesh);

	const FGenerateStaticMeshLODProcessSettings& GetCurrentSettings() const { return CurrentSettings; }
	void UpdateSettings(const FGenerateStaticMeshLODProcessSettings& NewSettings);


	UStaticMesh* GetSourceStaticMesh() const { return SourceStaticMesh; }
	const FDynamicMesh3& GetSourceMesh() const { return SourceMesh; }

	const FString& GetSourceAssetPath() const { return SourceAssetPath; }
	const FString& GetSourceAssetFolder() const { return SourceAssetFolder; }
	const FString& GetSourceAssetName() const { return SourceAssetName; }
	static FString GetDefaultDerivedAssetSuffix() { return TEXT("_AutoLOD"); }

	void CalculateDerivedPathName(FString NewAssetSuffix);

	bool ComputeDerivedSourceData();
	const FDynamicMesh3& GetDerivedLOD0Mesh() const { return DerivedLODMesh; }
	const FMeshTangentsd& GetDerivedLOD0MeshTangents() const { return DerivedLODMeshTangents; }
	const FSimpleShapeSet3d& GetDerivedCollision() const { return DerivedCollision; }

	/**
	 * Creates new Asset
	 */
	bool WriteDerivedAssetData();


	/**
	 * Updates existing SM Asset
	 */
	void UpdateSourceAsset();



	struct FPreviewMaterials
	{
		TArray<UMaterialInterface*> Materials;
		TArray<UTexture2D*> Textures;
	};
	void GetDerivedMaterialsPreview(FPreviewMaterials& MaterialSetOut);

	bool bUseParallelExecutor = false;

protected:

	UStaticMesh* SourceStaticMesh;
	FString SourceAssetPath;
	FString SourceAssetFolder;
	FString SourceAssetName;

	FDynamicMesh3 SourceMesh;

	struct FTextureInfo
	{
		UTexture2D* SourceTexture;
		FName ParameterName;
		FImageDimensions Dimensions;
		TImageBuilder<FVector4f> Image;
		bool bIsNormalMap = false;
		bool bIsDefaultTexture = false;
		bool bShouldBakeTexture = false;
	};

	struct FMaterialInfo
	{
		FStaticMaterial SourceMaterial;
		TArray<FTextureInfo> SourceTextures;
	};

	TArray<FMaterialInfo> SourceMaterials;



	FString DerivedSuffix;
	FString DerivedAssetFolder;
	FString DerivedAssetPath;

	FDynamicMesh3 DerivedLODMesh;
	FMeshTangentsd DerivedLODMeshTangents;
	FSimpleShapeSet3d DerivedCollision;
	UE::GeometryFlow::FNormalMapImage DerivedNormalMapImage;
	TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>> DerivedTextureImages;
	TMap<UTexture2D*, int32> TextureToDerivedTexIndex;

	TArray<FMaterialInfo> DerivedMaterials;
	UTexture2D* DerivedNormalMapTex;

	TUniquePtr<FGenerateMeshLODGraph> Generator;
	bool InitializeGenerator();

	FGenerateStaticMeshLODProcessSettings CurrentSettings;

	bool WriteDerivedTexture(UTexture2D* SourceTexture, UTexture2D* DerivedTexture);
	bool WriteDerivedTexture(UTexture2D* DerivedTexture, FString BaseTexName);
	void WriteDerivedTextures();
	void WriteDerivedMaterials();
	void UpdateMaterialTextureParameters(UMaterialInstanceConstant* Material, FMaterialInfo& DerivedMaterialInfo);
	void WriteDerivedStaticMeshAsset();

	void UpdateSourceStaticMeshAsset();

	void UpdateMaterialTextureParameters(UMaterialInstanceDynamic* Material, const FMaterialInfo& SourceMaterialInfo, 
		const TMap<UTexture2D*,UTexture2D*>& PreviewTextures, UTexture2D* PreviewNormalMap);
};