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
struct FMeshDescription;
using UE::Geometry::FDynamicMesh3;

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

UENUM()
enum class EGenerateStaticMeshLODSimpleCollisionGeometryType : uint8
{
	// NOTE: This must be kept in sync with ESimpleCollisionGeometryType in GenerateSimpleCollisionNode.h

	AlignedBoxes,
	OrientedBoxes,
	MinimalSpheres,
	Capsules,
	ConvexHulls,
	SweptHulls,
	MinVolume,
	None
};

// NOTE: This must be kept in sync with FMeshSimpleShapeApproximation::EProjectedHullAxisMode in MeshSimpleShapeApproximation.h

UENUM()
enum class EGenerateStaticMeshLODProjectedHullAxisMode : uint8
{
	X = 0,
	Y = 1,
	Z = 2,
	SmallestBoxDimension = 3,
	SmallestVolume = 4
};


USTRUCT()
struct FGenerateStaticMeshLODProcessSettings
{
	GENERATED_BODY()

	// Filter settings

	/** Group layer to use for filtering out detail before processing */
	UPROPERTY(EditAnywhere, Category = DetailFilter, meta = (DisplayName = "Detail Filter Group Layer"))
	FName FilterGroupLayer = FName(TEXT("PreFilterGroups"));

	// Thicken settings

	/** Weight map used during mesh thickening */
	UPROPERTY(EditAnywhere, Category = DetailFilter, meta = (DisplayName = "Thicken Weight Map"))
	FName ThickenWeightMapName = FName(TEXT("ThickenWeightMap"));

	/** Amount to thicken the mesh prior to Solidifying. The thicken weight map values are multiplied by this value. */
	UPROPERTY(EditAnywhere, Category = DetailFilter, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	float ThickenAmount = 0.0f;

	// Solidify settings

	/** Target number of voxels along the maximum dimension for Solidify operation */
	UPROPERTY(EditAnywhere, Category = Solidify, meta = (UIMin = "8", UIMax = "1024", ClampMin = "8", ClampMax = "1024", DisplayName="Voxel Resolution"))
	int SolidifyVoxelResolution = 64;

	/** Winding number threshold to determine what is considered inside the mesh during Solidify */
	UPROPERTY(EditAnywhere, Category = Solidify, AdvancedDisplay, meta = (UIMin = "0.1", UIMax = ".9", ClampMin = "-10", ClampMax = "10"))
	float WindingThreshold = 0.5f;


	// Morphology settings

	/** Offset distance in the Morpohological Closure operation */
	UPROPERTY(EditAnywhere, Category = Morphology, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000", DisplayName = "Closure Distance"))
	float ClosureDistance = 1.0f;


	// Simplify settings
	/** Target triangle count after simplification */
	UPROPERTY(EditAnywhere, Category = Simplify, meta = (UIMin = "1", ClampMin = "1", DisplayName = "Simplify Tri Count"))
	int SimplifyTriangleCount = 500;


	// UV settings
	/** Maximum number of charts to create in AutoUV */
	UPROPERTY(EditAnywhere, Category = AutoUV, meta = (DisplayName = "Max AutoUV Charts", UIMin = 0, UIMax = 1000))
	int NumAutoUVCharts = 0;


	// Bake Settings
	/** Resolution for normal map and texture baking */
	UPROPERTY(EditAnywhere, Category = Baking , meta = (DisplayName = "Bake Image Res"))
	EGenerateStaticMeshLODBakeResolution BakeResolution = EGenerateStaticMeshLODBakeResolution::Resolution512;

	/** How far away from the output mesh to search for input mesh during baking */
	UPROPERTY(EditAnywhere, Category = Baking, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000", DisplayName = "Bake Thickness"))
	float BakeThickness = 5.0f;

	UPROPERTY(EditAnywhere, Category = Baking, meta = (DisplayName = "Combine Textures"))
	bool bCombineTextures = true;


	// Simple collision generator settings

	// Transient property, not set directly by the user. The user controls a CollisionGroupLayerName dropdown property
	// on the Tool and that value is copied here.
	UPROPERTY(meta = (TransientToolProperty))
	FName CollisionGroupLayerName = TEXT("Default");

	/** Type of simple collision objects to produce */
	UPROPERTY(EditAnywhere, Category = Collision, meta = (DisplayName = "Collision Type"))
	EGenerateStaticMeshLODSimpleCollisionGeometryType CollisionType = EGenerateStaticMeshLODSimpleCollisionGeometryType::ConvexHulls;

	// Convex Hull Settings
	/** Target triangle count for each convex hull after simplification */
	UPROPERTY(EditAnywhere, Category = Collision, meta = (NoSpinbox = "true", DisplayName = "Convex Tri Count",
														  EditCondition = "CollisionType == EGenerateStaticMeshLODSimpleCollisionGeometryType::ConvexHulls"))
	int ConvexTriangleCount = 50;

	/** Whether to subsample input vertices using a regular grid before computing the convex hull */
	UPROPERTY(EditAnywhere, Category = Collision, meta = (EditCondition = "CollisionType == EGenerateStaticMeshLODSimpleCollisionGeometryType::ConvexHulls"))
	bool bPrefilterVertices = true;

	/** Grid resolution (along the maximum-length axis) for subsampling before computing the convex hull */
	UPROPERTY(EditAnywhere, Category = Collision, meta = (NoSpinbox = "true", EditCondition = "CollisionType == EGenerateStaticMeshLODSimpleCollisionGeometryType::ConvexHulls && bPrefilterVertices", UIMin = 4, UIMax = 100))
	int PrefilterGridResolution = 10;


	// Swept Convex Hull Settings
	/** Whether to simplify polygons used for swept convex hulls */
	UPROPERTY(EditAnywhere, Category = Collision, meta = (EditCondition = "CollisionType == EGenerateStaticMeshLODSimpleCollisionGeometryType::SweptHulls"))
	bool bSimplifyPolygons = true;

	/** Target minumum edge length for simplified swept convex hulls */
	UPROPERTY(EditAnywhere, Category = Collision, meta = (NoSpinbox = "true", UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "100000",
														  EditCondition = "CollisionType == EGenerateStaticMeshLODSimpleCollisionGeometryType::SweptHulls"))
	float HullTolerance = 0.1;

	/** Which axis to sweep along when computing swept convex hulls */
	UPROPERTY(EditAnywhere, Category = Collision, meta = (EditCondition = "CollisionType == EGenerateStaticMeshLODSimpleCollisionGeometryType::SweptHulls"))
	EGenerateStaticMeshLODProjectedHullAxisMode SweepAxis = EGenerateStaticMeshLODProjectedHullAxisMode::SmallestVolume;

};


UCLASS(Transient)
class UGenerateStaticMeshLODProcess : public UObject
{
	GENERATED_BODY()
public:

	bool Initialize(UStaticMesh* SourceMesh, FProgressCancel* Progress = nullptr);

	const FGenerateStaticMeshLODProcessSettings& GetCurrentSettings() const { return CurrentSettings; }
	void UpdateSettings(const FGenerateStaticMeshLODProcessSettings& NewSettings);


	UStaticMesh* GetSourceStaticMesh() const { return SourceStaticMesh; }
	const FDynamicMesh3& GetSourceMesh() const { return SourceMesh; }

	const FString& GetSourceAssetPath() const { return SourceAssetPath; }
	const FString& GetSourceAssetFolder() const { return SourceAssetFolder; }
	const FString& GetSourceAssetName() const { return SourceAssetName; }

	static FString GetDefaultDerivedAssetSuffix() { return TEXT("_AutoLOD"); }
	const FString& GetDerivedAssetName() const { return DerivedAssetName; }

	void CalculateDerivedPathName(const FString& NewAssetBaseName, const FString& NewAssetSuffix);

	bool ComputeDerivedSourceData(FProgressCancel* Progress);
	const FDynamicMesh3& GetDerivedLOD0Mesh() const { return DerivedLODMesh; }
	const UE::Geometry::FMeshTangentsd& GetDerivedLOD0MeshTangents() const { return DerivedLODMeshTangents; }
	const UE::Geometry::FSimpleShapeSet3d& GetDerivedCollision() const { return DerivedCollision; }

	/**
	 * Creates new Asset
	 */
	virtual bool WriteDerivedAssetData();


	/**
	 * Updates existing SM Asset
	 */
	virtual void UpdateSourceAsset(bool bSetNewHDSourceAsset = false);



	struct FPreviewMaterials
	{
		TArray<UMaterialInterface*> Materials;
		TArray<UTexture2D*> Textures;
	};
	void GetDerivedMaterialsPreview(FPreviewMaterials& MaterialSetOut);

	FCriticalSection GraphEvalCriticalSection;

protected:

	UPROPERTY()
	UStaticMesh* SourceStaticMesh;

	FString SourceAssetPath;
	FString SourceAssetFolder;
	FString SourceAssetName;

	// if true, we are building new LOD0 from the StaticMesh HiRes SourceModel, instead of from the mesh in LOD0
	bool bUsingHiResSource = false;

	// copy of input MeshDescription with autogenerated attributes computed
	TSharedPtr<FMeshDescription> SourceMeshDescription;
	// SourceMeshDescription converted to FDynamicMesh3
	FDynamicMesh3 SourceMesh;

	struct FTextureInfo
	{
		UTexture2D* Texture = nullptr;
		FName ParameterName;
		UE::Geometry::FImageDimensions Dimensions;
		UE::Geometry::TImageBuilder<FVector4f> Image;
		bool bIsNormalMap = false;
		bool bIsDefaultTexture = false;
		bool bShouldBakeTexture = false;
		bool bIsUsedInMultiTextureBaking = false;
	};

	int SelectTextureToBake(const TArray<FTextureInfo>& TextureInfos) const;

	// Information about one of the input StaticMesh Materials. Computed in Initialize() and not modified afterwards
	struct FSourceMaterialInfo
	{
		FStaticMaterial SourceMaterial;
		TArray<FTextureInfo> SourceTextures;

		bool bHasNormalMap = false;						// if true, Material has an exposed NormalMap input texture parameter
		bool bHasTexturesToBake = false;				// if true, Material has at least one SourceTexture that should be baked
		bool bIsReusable = false;						// if true, Material doesn't need any texture baking and can be re-used by LOD0
		bool bIsPreviouslyGeneratedMaterial = false;	// if true, this Material was previously generated by AutoLOD and should be discarded. 
														// Currently inferred from material being in LOD0 but not HiRes Source
	};

	// list of initial Source Materials, length equivalent to StaticMesh.StaticMaterials
	TArray<FSourceMaterialInfo> SourceMaterials;

	FString DerivedSuffix;
	FString DerivedAssetPath;
	FString DerivedAssetFolder;
	FString DerivedAssetName;
	FString DerivedAssetNameNoSuffix;
	

	FDynamicMesh3 DerivedLODMesh;				// the new generated LOD0 mesh
	UE::Geometry::FMeshTangentsd DerivedLODMeshTangents;		// Tangents for DerivedLODMesh
	UE::Geometry::FSimpleShapeSet3d DerivedCollision;			// Simple Collision for DerivedLODMesh

	// Texture set potentially required by output Material set
	UE::GeometryFlow::FNormalMapImage DerivedNormalMapImage;	// Normal Map
	TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>> DerivedTextureImages;	// generated Textures
	UE::GeometryFlow::FTextureImage DerivedMultiTextureBakeImage;
	TMap<UTexture2D*, int32> SourceTextureToDerivedTexIndex;	// mapping from input Textures to DerivedTextureImages index

	// Information about an output Material
	struct FDerivedMaterialInfo
	{
		int32 SourceMaterialIndex = -1;				// Index into SourceMaterials
		bool bUseSourceMaterialDirectly = false;	// If true, do not create/use a Derived Material, directly re-use SourceMaterials[SourceMaterialIndex]

		FStaticMaterial DerivedMaterial;			// points to generated Material
		TArray<FTextureInfo> DerivedTextures;		// List of generated Textures
	};

	// list of generated/Derived Materials. Length is the same as SourceMaterials and indices correspond, however some Derived Materials may not be initialized (if Source is reusable or generated)
	TArray<FDerivedMaterialInfo> DerivedMaterials;

	// This list is for accumulating derived UTexture2D's created during WriteDerivedTextures(). We have to
	// maintain uproperty references to these or they may be garbage collected
	UPROPERTY()
	TSet<UTexture2D*> AllDerivedTextures;

	// Derived Normal Map
	UPROPERTY()
	UTexture2D* DerivedNormalMapTex;

	// For each material participating in multi-texture baking, the parameter name of the texture
	TMap<int32, FName> MultiTextureParameterName;

	UPROPERTY()
	UTexture2D* DerivedMultiTextureBakeResult;


	TUniquePtr<FGenerateMeshLODGraph> Generator;			// active LODGenerator Graph
	bool InitializeGenerator();

	FGenerateStaticMeshLODProcessSettings CurrentSettings;

	bool WriteDerivedTexture(UTexture2D* SourceTexture, UTexture2D* DerivedTexture, bool bCreatingNewStaticMeshAsset);
	bool WriteDerivedTexture(UTexture2D* DerivedTexture, FString BaseTexName, bool bCreatingNewStaticMeshAsset);
	void WriteDerivedTextures(bool bCreatingNewStaticMeshAsset);
	void WriteDerivedMaterials(bool bCreatingNewStaticMeshAsset);
	void UpdateMaterialTextureParameters(UMaterialInstanceConstant* Material, FDerivedMaterialInfo& DerivedMaterialInfo);
	void WriteDerivedStaticMeshAsset();

	void UpdateSourceStaticMeshAsset(bool bSetNewHDSourceAsset);

	void UpdateMaterialTextureParameters(UMaterialInstanceDynamic* Material, const FSourceMaterialInfo& SourceMaterialInfo,
		const TMap<UTexture2D*,UTexture2D*>& PreviewTextures, UTexture2D* PreviewNormalMap);

	// Return true if the given path corresponds to a material or texture in SourceMaterials
	bool IsSourceAsset(const FString& AssetPath) const;
};
