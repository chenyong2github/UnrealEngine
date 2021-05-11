// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Crc.h"
#include "Rendering/NaniteResources.h"
#include "InstanceUniformShaderParameters.h"
#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "Chaos/ChaosSolverActor.h"

#include "GeometryCollectionObject.generated.h"

class UMaterialInterface;
class UGeometryCollectionCache;
class FGeometryCollection;
class FManagedArrayCollection;
struct FGeometryCollectionSection;
struct FSharedSimulationParameters;

USTRUCT(BlueprintType)
struct GEOMETRYCOLLECTIONENGINE_API FGeometryCollectionSource
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GeometrySource", meta=(AllowedClasses="StaticMesh, SkeletalMesh, GeometryCollection"))
	FSoftObjectPath SourceGeometryObject;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GeometrySource")
	FTransform LocalTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GeometrySource")
	TArray<TObjectPtr<UMaterialInterface>> SourceMaterial;
};

USTRUCT(BlueprintType)
struct GEOMETRYCOLLECTIONENGINE_API FGeometryCollectionEmbeddedExemplar
{
	GENERATED_BODY()

	FGeometryCollectionEmbeddedExemplar() 
		: StaticMeshExemplar(FString(TEXT("None")))
		, StartCullDistance(0.0f)
		, EndCullDistance(0.0f)
		, InstanceCount(0)
	{ };
	
	FGeometryCollectionEmbeddedExemplar(FSoftObjectPath NewExemplar)
		: StaticMeshExemplar(NewExemplar)
		, StartCullDistance(0.0f)
		, EndCullDistance(0.0f)
		, InstanceCount(0)
	{ }

	UPROPERTY(EditAnywhere, Category = "EmbeddedExemplar", meta = (AllowedClasses = "StaticMesh"))
	FSoftObjectPath StaticMeshExemplar;

	UPROPERTY(EditAnywhere, Category = "EmbeddedExemplar")
	float StartCullDistance;

	UPROPERTY(EditAnywhere, Category = "EmbeddedExemplar")
	float EndCullDistance;

	UPROPERTY(VisibleAnywhere, Category = "EmbeddedExemplar")
	int32 InstanceCount;
};


USTRUCT()
struct GEOMETRYCOLLECTIONENGINE_API FGeometryCollectionSizeSpecificData
{
	GENERATED_BODY()

	FGeometryCollectionSizeSpecificData();

	/** The max size these settings apply to*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	float MaxSize;

	/*
	*  CollisionType defines how to initialize the rigid collision structures.
	*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	ECollisionTypeEnum CollisionType;

	/*
	*  CollisionType defines how to initialize the rigid collision structures.
	*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	EImplicitTypeEnum ImplicitType;

	/*
	*  Resolution on the smallest axes for the level set. (def: 5)
	*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	int32 MinLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 10)
	*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	int32 MaxLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 5)
	*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	int32 MinClusterLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 10)
	*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	int32 MaxClusterLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 10)
	*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	int32 CollisionObjectReductionPercentage;

	/**
	 * Number of particles on the triangulated surface to use for collisions.
	 */
	UPROPERTY(EditAnywhere, Category = "Collisions")
	float CollisionParticlesFraction;

	/**
	 * Max number of particles.
	 */
	UPROPERTY(EditAnywhere, Category = "Collisions")
	int32 MaximumCollisionParticles;

	/**
	 * Max number of particles.
	*/
	UPROPERTY(EditAnywhere, Category = "Collisions")
	int32 DamageThreshold;

};

class FGeometryCollectionNaniteData
{
public:
	GEOMETRYCOLLECTIONENGINE_API FGeometryCollectionNaniteData();
	GEOMETRYCOLLECTIONENGINE_API ~FGeometryCollectionNaniteData();

	FORCEINLINE bool IsInitialized()
	{
		return bIsInitialized;
	}

	/** Serialization. */
	void Serialize(FArchive& Ar, UGeometryCollection* Owner);

	/** Initialize the render resources. */
	void InitResources(UGeometryCollection* Owner);

	/** Releases the render resources. */
	GEOMETRYCOLLECTIONENGINE_API void ReleaseResources();

	Nanite::FResources NaniteResource;

private:
	bool bIsInitialized = false;
};

/**
* UGeometryCollectionObject (UObject)
*
* UObject wrapper for the FGeometryCollection
*
*/
UCLASS(BlueprintType, customconstructor)
class GEOMETRYCOLLECTIONENGINE_API UGeometryCollection : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UGeometryCollection(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** UObject Interface */
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
#endif
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	/** End UObject Interface */

	void Serialize(FArchive& Ar);

#if WITH_EDITOR
	void EnsureDataIsCooked(bool bInitResources = true);
#endif

	/** Accessors for internal geometry collection */
	void SetGeometryCollection(TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionIn) { GeometryCollection = GeometryCollectionIn; }
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>       GetGeometryCollection() { return GeometryCollection; }
	const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GetGeometryCollection() const { return GeometryCollection; }

	/** Return collection to initial (ie. empty) state. */
	void Reset();
	
	int32 AppendGeometry(const UGeometryCollection & Element, bool ReindexAllMaterials = false, const FTransform& TransformRoot = FTransform::Identity);
	int32 NumElements(const FName& Group) const;
	void RemoveElements(const FName& Group, const TArray<int32>& SortedDeletionList);

	FNaniteInfo GetNaniteInfo(int32 GeometryIndex) const
	{
		check(NaniteData);
		Nanite::FResources& Resource = NaniteData->NaniteResource;
		check(GeometryIndex >= 0 && GeometryIndex < Resource.HierarchyRootOffsets.Num());
		bool bHasImposter = Resource.ImposterAtlas.Num() > 0;
		return FNaniteInfo(Resource.RuntimeResourceID, Resource.HierarchyOffset + Resource.HierarchyRootOffsets[GeometryIndex], bHasImposter);
	}

	/** ReindexMaterialSections */
	void ReindexMaterialSections();

	/** appends the standard materials to this uobject */
	void InitializeMaterials();


	/** Returns true if there is anything to render */
	bool HasVisibleGeometry() const;

	/** Invalidates this collection signaling a structural change and renders any previously recorded caches unable to play with this collection */
	void InvalidateCollection();

	/** Check to see if Simulation Data requires regeneration */
	bool IsSimulationDataDirty() const;

	/** Attach a Static Mesh exemplar for embedded geometry, if that mesh has not already been attached. Return the exemplar index. */
	int32 AttachEmbeddedGeometryExemplar(const UStaticMesh* Exemplar);

	/** Remove embedded geometry exemplars with indices matching the sorted removal list. */
	void RemoveExemplars(const TArray<int32>& SortedRemovalIndices);

	/** Produce a deep copy of GeometryCollection member, stripped of data unecessary for gameplay. */
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GenerateMinimalGeometryCollection() const;

#if WITH_EDITOR
	/** If this flag is set, we only regenerate simulation data when requested via CreateSimulationData() */
	bool bManualDataCreate;
	
	/** Create the simulation data that can be shared among all instances (mass, volume, etc...)*/
	void CreateSimulationData();

	/** Create the Nanite rendering data. */
	static TUniquePtr<FGeometryCollectionNaniteData> CreateNaniteData(FGeometryCollection* Collection);
#endif

	void InitResources();
	void ReleaseResources();

	/** Fills params struct with parameters used for precomputing content. */
	void GetSharedSimulationParams(FSharedSimulationParameters& OutParams) const;
	void FixupRemoveOnFractureMaterials(FSharedSimulationParameters& SharedParms) const;

	/** Accessors for the two guids used to identify this collection */
	FGuid GetIdGuid() const;
	FGuid GetStateGuid() const;

	/** Pointer to the data used to render this geometry collection with Nanite. */
	TUniquePtr<class FGeometryCollectionNaniteData> NaniteData;

	UPROPERTY(EditAnywhere, Category = "Clustering")
	bool EnableClustering;

	/** Maximum level for cluster breaks. */
	UPROPERTY(EditAnywhere, Category = "Clustering")
	int32 ClusterGroupIndex;

	/** Maximum level for cluster breaks. */
	UPROPERTY(EditAnywhere, Category = "Clustering")
	int32 MaxClusterLevel;

	/** Damage threshold for clusters at different levels. */
	UPROPERTY(EditAnywhere, Category = "Clustering")
	TArray<float> DamageThreshold;

	/** */
	UPROPERTY(EditAnywhere, Category = "Clustering")
	EClusterConnectionTypeEnum ClusterConnectionType;

#if WITH_EDITORONLY_DATA
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GeometrySource")
	TArray<FGeometryCollectionSource> GeometrySource;
#endif

	UPROPERTY(EditAnywhere, Category = "Materials")
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	/** References for embedded geometry generation */
	UPROPERTY(EditAnywhere, Category = "EmbeddedGeometry")
	TArray<FGeometryCollectionEmbeddedExemplar> EmbeddedGeometryExemplar;

	/**
	* Enable support for Nanite.
	*/
	UPROPERTY(EditAnywhere, Category = "Nanite")
	bool bStripOnCook;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nanite")
	bool EnableNanite;

	/*
	*  CollisionType defines how to initialize the rigid collision structures.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	ECollisionTypeEnum CollisionType;

	/*
	*  CollisionType defines how to initialize the rigid collision structures.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	EImplicitTypeEnum ImplicitType;

	/*
	*  Resolution on the smallest axes for the level set. (def: 5)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	int32 MinLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 10)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	int32 MaxLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 5)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	int32 MinClusterLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 10)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	int32 MaxClusterLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 10)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	float CollisionObjectReductionPercentage;

	/**
	* Mass As Density, units are in kg/m^3
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	bool bMassAsDensity;

	/**
	* Total Mass of Collection. If density, units are in kg/m^3
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	float Mass;

	/**
	* Smallest allowable mass (def:0.1)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	float MinimumMassClamp;

	/**
	 * Number of particles on the triangulated surface to use for collisions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	float CollisionParticlesFraction;

	/**
	 * Max number of particles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collisions")
	int32 MaximumCollisionParticles;

	UPROPERTY(EditAnywhere, Category = "Collisions")
	TArray<FGeometryCollectionSizeSpecificData> SizeSpecificData;

	/**
	* Enable remove pieces on fracture
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fracture")
	bool EnableRemovePiecesOnFracture;

	/**
	* Materials relating to remove on fracture
	*/
	UPROPERTY(EditAnywhere, Category = "Fracture")
	TArray<TObjectPtr<UMaterialInterface>> RemoveOnFractureMaterials;

	FORCEINLINE const int32 GetBoneSelectedMaterialIndex() const { return BoneSelectedMaterialIndex; }

	/** Returns the asset path for the automatically populated selected material. */
	static const TCHAR* GetSelectedMaterialPath();

#if WITH_EDITORONLY_DATA
	/** Information for thumbnail rendering */
	UPROPERTY(VisibleAnywhere, Instanced, AdvancedDisplay, Category = GeometryCollection)
	TObjectPtr<class UThumbnailInfo> ThumbnailInfo;
#endif // WITH_EDITORONLY_DATA

private:
#if WITH_EDITOR
	void CreateSimulationDataImp(bool bCopyFromDDC);

#endif

private:
	/** Guid created on construction of this collection. It should be used to uniquely identify this collection */
	UPROPERTY()
	FGuid PersistentGuid;

	/** 
	 * Guid that can be invalidated on demand - essentially a 'version' that should be changed when a structural change is made to
	 * the geometry collection. This signals to any caches that attempt to link to a geometry collection whether the collection
	 * is still valid (hasn't structurally changed post-recording)
	 */
	UPROPERTY()
	FGuid StateGuid;

#if WITH_EDITOR
	//Used to determine whether we need to cook content
	FGuid LastBuiltGuid;

	//Used to determine whether we need to regenerate simulation data
	FGuid SimulationDataGuid;
#endif

	// #todo(dmp): rename to be consistent BoneSelectedMaterialID?
	UPROPERTY()
	int32 BoneSelectedMaterialIndex;

	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollection;
};
