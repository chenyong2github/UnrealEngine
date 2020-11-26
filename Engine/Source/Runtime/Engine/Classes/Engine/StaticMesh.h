// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "UObject/ScriptMacros.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "RenderCommandFence.h"
#include "Components.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Interfaces/Interface_AsyncCompilation.h"
#include "Engine/MeshMerging.h"
#include "Engine/StreamableRenderAsset.h"
#include "Templates/UniquePtr.h"
#include "StaticMeshResources.h"
#include "PerPlatformProperties.h"
#include "RenderAssetUpdate.h"
#include "MeshTypes.h"

#include "StaticMesh.generated.h"

class FSpeedTreeWind;
class UAssetUserData;
class UMaterialInterface;
class UNavCollisionBase;
class UStaticMeshComponent;
class UStaticMeshDescription;
class FStaticMeshUpdate;
struct FMeshDescription;
struct FMeshDescriptionBulkData;
struct FStaticMeshLODResources;

/*-----------------------------------------------------------------------------
	Async Static Mesh Compilation
-----------------------------------------------------------------------------*/

enum class EStaticMeshAsyncProperties : uint32
{
	None                    = 0,
	RenderData              = 1 << 0,
	OccluderData            = 1 << 1,
	SourceModels            = 1 << 2,
	SectionInfoMap          = 1 << 3,
	OriginalSectionInfoMap  = 1 << 4,
	NavCollision            = 1 << 5,
	LightmapUVVersion       = 1 << 6,
	BodySetup               = 1 << 7,
	LightingGuid            = 1 << 8,
	ExtendedBounds          = 1 << 9,
	NegativeBoundsExtension = 1 << 10,
	PositiveBoundsExtension = 1 << 11,
	StaticMaterials         = 1 << 12,
	LightmapUVDensity       = 1 << 13,
	IsBuiltAtRuntime        = 1 << 14,
	MinLOD                  = 1 << 15,
	LightMapCoordinateIndex = 1 << 16,
	LightMapResolution      = 1 << 17,

	All                     = MAX_uint32
};

inline const TCHAR* ToString(EStaticMeshAsyncProperties Value)
{
	switch (Value)
	{
		case EStaticMeshAsyncProperties::None: 
			return TEXT("None");
		case EStaticMeshAsyncProperties::RenderData: 
			return TEXT("RenderData");
		case EStaticMeshAsyncProperties::OccluderData: 
			return TEXT("OccluderData");
		case EStaticMeshAsyncProperties::SourceModels: 
			return TEXT("SourceModels");
		case EStaticMeshAsyncProperties::SectionInfoMap: 
			return TEXT("SectionInfoMap");
		case EStaticMeshAsyncProperties::OriginalSectionInfoMap:
			return TEXT("OriginalSectionInfoMap");
		case EStaticMeshAsyncProperties::NavCollision: 
			return TEXT("NavCollision");
		case EStaticMeshAsyncProperties::LightmapUVVersion: 
			return TEXT("LightmapUVVersion");
		case EStaticMeshAsyncProperties::BodySetup: 
			return TEXT("BodySetup");
		case EStaticMeshAsyncProperties::LightingGuid: 
			return TEXT("LightingGuid");
		case EStaticMeshAsyncProperties::ExtendedBounds: 
			return TEXT("ExtendedBounds");
		case EStaticMeshAsyncProperties::NegativeBoundsExtension:
			return TEXT("NegativeBoundsExtension");
		case EStaticMeshAsyncProperties::PositiveBoundsExtension:
			return TEXT("PositiveBoundsExtension");
		case EStaticMeshAsyncProperties::StaticMaterials: 
			return TEXT("StaticMaterials");
		case EStaticMeshAsyncProperties::LightmapUVDensity: 
			return TEXT("LightmapUVDensity");
		case EStaticMeshAsyncProperties::IsBuiltAtRuntime: 
			return TEXT("IsBuiltAtRuntime");
		case EStaticMeshAsyncProperties::MinLOD:
			return TEXT("MinLOD");
		case EStaticMeshAsyncProperties::LightMapCoordinateIndex:
			return TEXT("LightMapCoordinateIndex");
		case EStaticMeshAsyncProperties::LightMapResolution:
			return TEXT("LightMapResolution");
		default: 
			check(false); 
			return TEXT("Unknown");
	}
}

ENUM_CLASS_FLAGS(EStaticMeshAsyncProperties);

class FStaticMeshPostLoadContext;
class FStaticMeshBuildContext;

#if WITH_EDITOR

// Any thread implicated in the static mesh build must have a valid scope to be granted access to protected properties without causing any stalls.
class FStaticMeshAsyncBuildScope
{
public:
	FStaticMeshAsyncBuildScope(const UStaticMesh* StaticMesh)
	{
		PreviousScope = StaticMeshBeingAsyncCompiled;
		StaticMeshBeingAsyncCompiled = StaticMesh;
	}

	~FStaticMeshAsyncBuildScope()
	{
		check(StaticMeshBeingAsyncCompiled);
		StaticMeshBeingAsyncCompiled = PreviousScope;
	}

	static bool ShouldWaitOnLockedProperties(const UStaticMesh* StaticMesh)
	{
		return StaticMeshBeingAsyncCompiled != StaticMesh;
	}

private:
	const UStaticMesh* PreviousScope = nullptr;
	// Only the thread(s) compiling this static mesh will have full access to protected properties without causing any stalls.
	static thread_local const UStaticMesh* StaticMeshBeingAsyncCompiled;
};

/**
 * Worker used to perform async static mesh compilation.
 */
class FStaticMeshAsyncBuildWorker : public FNonAbandonableTask
{
public:
	UStaticMesh* StaticMesh;
	TUniquePtr<FStaticMeshPostLoadContext> PostLoadContext;
	TUniquePtr<FStaticMeshBuildContext> BuildContext;

	/** Initialization constructor. */
	FStaticMeshAsyncBuildWorker(
		UStaticMesh* InStaticMesh,
		TUniquePtr<FStaticMeshBuildContext>&& InBuildContext)
		: StaticMesh(InStaticMesh)
		, PostLoadContext(nullptr)
		, BuildContext(MoveTemp(InBuildContext))
	{
	}

	/** Initialization constructor. */
	FStaticMeshAsyncBuildWorker(
		UStaticMesh* InStaticMesh,
		TUniquePtr<FStaticMeshPostLoadContext>&& InPostLoadContext)
		: StaticMesh(InStaticMesh)
		, PostLoadContext(MoveTemp(InPostLoadContext))
		, BuildContext(nullptr)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FStaticMeshAsyncBuildWorker, STATGROUP_ThreadPoolAsyncTasks);
	}

	void DoWork();
};

struct FStaticMeshAsyncBuildTask : public FAsyncTask<FStaticMeshAsyncBuildWorker>
{
	FStaticMeshAsyncBuildTask(
		UStaticMesh* InStaticMesh,
		TUniquePtr<FStaticMeshPostLoadContext>&& InPostLoadContext)
		: FAsyncTask<FStaticMeshAsyncBuildWorker>(InStaticMesh, MoveTemp(InPostLoadContext))
		, StaticMesh(InStaticMesh)
	{
	}

	FStaticMeshAsyncBuildTask(
		UStaticMesh* InStaticMesh,
		TUniquePtr<FStaticMeshBuildContext>&& InBuildContext)
		: FAsyncTask<FStaticMeshAsyncBuildWorker>(InStaticMesh, MoveTemp(InBuildContext))
		, StaticMesh(InStaticMesh)
	{
	}

	const UStaticMesh* StaticMesh;
};
#endif // #if WITH_EDITOR

/*-----------------------------------------------------------------------------
	Legacy mesh optimization settings.
-----------------------------------------------------------------------------*/

/** Optimization settings used to simplify mesh LODs. */
UENUM()
enum ENormalMode
{
	NM_PreserveSmoothingGroups,
	NM_RecalculateNormals,
	NM_RecalculateNormalsSmooth,
	NM_RecalculateNormalsHard,
	TEMP_BROKEN,
	ENormalMode_MAX,
};

UENUM()
enum EImportanceLevel
{
	IL_Off,
	IL_Lowest,
	IL_Low,
	IL_Normal,
	IL_High,
	IL_Highest,
	TEMP_BROKEN2,
	EImportanceLevel_MAX,
};

/** Enum specifying the reduction type to use when simplifying static meshes. */
UENUM()
enum EOptimizationType
{
	OT_NumOfTriangles,
	OT_MaxDeviation,
	OT_MAX,
};

/** Old optimization settings. */
USTRUCT()
struct FStaticMeshOptimizationSettings
{
	GENERATED_USTRUCT_BODY()

	/** The method to use when optimizing the skeletal mesh LOD */
	UPROPERTY()
	TEnumAsByte<enum EOptimizationType> ReductionMethod;

	/** If ReductionMethod equals SMOT_NumOfTriangles this value is the ratio of triangles [0-1] to remove from the mesh */
	UPROPERTY()
	float NumOfTrianglesPercentage;

	/**If ReductionMethod equals SMOT_MaxDeviation this value is the maximum deviation from the base mesh as a percentage of the bounding sphere. */
	UPROPERTY()
	float MaxDeviationPercentage;

	/** The welding threshold distance. Vertices under this distance will be welded. */
	UPROPERTY()
	float WeldingThreshold;

	/** Whether Normal smoothing groups should be preserved. If false then NormalsThreshold is used **/
	UPROPERTY()
	bool bRecalcNormals;

	/** If the angle between two triangles are above this value, the normals will not be
	smooth over the edge between those two triangles. Set in degrees. This is only used when PreserveNormals is set to false*/
	UPROPERTY()
	float NormalsThreshold;

	/** How important the shape of the geometry is (EImportanceLevel). */
	UPROPERTY()
	uint8 SilhouetteImportance;

	/** How important texture density is (EImportanceLevel). */
	UPROPERTY()
	uint8 TextureImportance;

	/** How important shading quality is. */
	UPROPERTY()
	uint8 ShadingImportance;


	FStaticMeshOptimizationSettings()
	: ReductionMethod( OT_MaxDeviation )
	, NumOfTrianglesPercentage( 1.0f )
	, MaxDeviationPercentage( 0.0f )
	, WeldingThreshold( 0.1f )
	, bRecalcNormals( true )
	, NormalsThreshold( 60.0f )
	, SilhouetteImportance( IL_Normal )
	, TextureImportance( IL_Normal )
	, ShadingImportance( IL_Normal )
	{
	}

	/** Serialization for FStaticMeshOptimizationSettings. */
	inline friend FArchive& operator<<( FArchive& Ar, FStaticMeshOptimizationSettings& Settings )
	{
		Ar << Settings.ReductionMethod;
		Ar << Settings.MaxDeviationPercentage;
		Ar << Settings.NumOfTrianglesPercentage;
		Ar << Settings.SilhouetteImportance;
		Ar << Settings.TextureImportance;
		Ar << Settings.ShadingImportance;
		Ar << Settings.bRecalcNormals;
		Ar << Settings.NormalsThreshold;
		Ar << Settings.WeldingThreshold;

		return Ar;
	}

};

/*-----------------------------------------------------------------------------
	UStaticMesh
-----------------------------------------------------------------------------*/

/**
 * Source model from which a renderable static mesh is built.
 */
USTRUCT()
struct FStaticMeshSourceModel
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITOR
	/**
	 * Imported raw mesh data. Optional for all but the first LOD.
	 *
	 * This is a member for legacy assets only.
	 * If it is non-empty, this means that it has been de-serialized from the asset, and
	 * the asset hence pre-dates MeshDescription.
	 */
	class FRawMeshBulkData* RawMeshBulkData;
	
	/*
	 * The staticmesh owner of this source model. We need the SM to be able to convert between MeshDesription and RawMesh.
	 * RawMesh use int32 material index and MeshDescription use FName material slot name.
	 * This memeber is fill in the PostLoad of the static mesh.
	 * TODO: Remove this member when FRawMesh will be remove.
	 */
	class UStaticMesh* StaticMeshOwner;
	/*
	 * Accessor to Load and save the raw mesh or the mesh description depending on the editor settings.
	 * Temporary until we deprecate the RawMesh.
	 */
	ENGINE_API bool IsRawMeshEmpty() const;
	ENGINE_API void LoadRawMesh(struct FRawMesh& OutRawMesh) const;
	ENGINE_API void SaveRawMesh(struct FRawMesh& InRawMesh, bool bConvertToMeshdescription = true);

#endif // #if WITH_EDITOR

#if WITH_EDITORONLY_DATA
	/**
	 * Mesh description unpacked from bulk data.
	 *
	 * If this is valid, this means the mesh description has either been unpacked from the bulk data stored in the asset,
	 * or one has been generated by the build tools (or converted from legacy RawMesh).
	 */
	TUniquePtr<FMeshDescription> MeshDescription;

	/**
	 * Bulk data containing mesh description. LOD0 must be valid, but autogenerated lower LODs may be invalid.
	 *
	 * New assets store their source data here instead of in the RawMeshBulkData.
	 * If this is invalid, either the LOD is autogenerated (for LOD1+), or the asset is a legacy asset whose
	 * data is in the RawMeshBulkData.
	 */
	TUniquePtr<FMeshDescriptionBulkData> MeshDescriptionBulkData;
#endif

	/** Settings applied when building the mesh. */
	UPROPERTY(EditAnywhere, Category=BuildSettings)
	FMeshBuildSettings BuildSettings;

	/** Reduction settings to apply when building render data. */
	UPROPERTY(EditAnywhere, Category=ReductionSettings)
	FMeshReductionSettings ReductionSettings; 

	UPROPERTY()
	float LODDistance_DEPRECATED;

	/** 
	 * ScreenSize to display this LOD.
	 * The screen size is based around the projected diameter of the bounding
	 * sphere of the model. i.e. 0.5 means half the screen's maximum dimension.
	 */
	UPROPERTY(EditAnywhere, Category=ReductionSettings)
	FPerPlatformFloat ScreenSize;

	/** The file path that was used to import this LOD. */
	UPROPERTY(VisibleAnywhere, Category = StaticMeshSourceModel, AdvancedDisplay)
	FString SourceImportFilename;

#if WITH_EDITORONLY_DATA
	/** Whether this LOD was imported in the same file as the base mesh. */
	UPROPERTY()
	bool bImportWithBaseMesh;
#endif

	/** Default constructor. */
	ENGINE_API FStaticMeshSourceModel();

	/** Destructor. */
	ENGINE_API ~FStaticMeshSourceModel();

#if WITH_EDITOR
	/** Serializes bulk data. */
	void SerializeBulkData(FArchive& Ar, UObject* Owner);

	/** Create a new MeshDescription object */
	FMeshDescription* CreateMeshDescription();
#endif
};

// Make FStaticMeshSourceModel non-assignable
template<> struct TStructOpsTypeTraits<FStaticMeshSourceModel> : public TStructOpsTypeTraitsBase2<FStaticMeshSourceModel> { enum { WithCopy = false }; };


/**
 * Per-section settings.
 */
USTRUCT()
struct FMeshSectionInfo
{
	GENERATED_USTRUCT_BODY()

	/** Index in to the Materials array on UStaticMesh. */
	UPROPERTY()
	int32 MaterialIndex;

	/** If true, collision is enabled for this section. */
	UPROPERTY()
	bool bEnableCollision;

	/** If true, this section will cast shadows. */
	UPROPERTY()
	bool bCastShadow;

	/** If true, this section will be visible in ray tracing Geometry. */
	UPROPERTY()
	bool bVisibleInRayTracing;

	/** If true, this section will always considered opaque in ray tracing Geometry. */
	UPROPERTY()
	bool bForceOpaque;

	/** Default values. */
	FMeshSectionInfo()
		: MaterialIndex(0)
		, bEnableCollision(true)
		, bCastShadow(true)
		, bVisibleInRayTracing(true)
		, bForceOpaque(false)
	{
	}

	/** Default values with an explicit material index. */
	explicit FMeshSectionInfo(int32 InMaterialIndex)
		: MaterialIndex(InMaterialIndex)
		, bEnableCollision(true)
		, bCastShadow(true)
		, bVisibleInRayTracing(true)
		, bForceOpaque(false)
	{
	}
};

/** Comparison for mesh section info. */
bool operator==(const FMeshSectionInfo& A, const FMeshSectionInfo& B);
bool operator!=(const FMeshSectionInfo& A, const FMeshSectionInfo& B);

/**
 * Map containing per-section settings for each section of each LOD.
 */
USTRUCT()
struct FMeshSectionInfoMap
{
	GENERATED_USTRUCT_BODY()

	/** Maps an LOD+Section to the material it should render with. */
	UPROPERTY()
	TMap<uint32,FMeshSectionInfo> Map;

	/** Serialize. */
	void Serialize(FArchive& Ar);

	/** Clears all entries in the map resetting everything to default. */
	ENGINE_API void Clear();

	/** Get the number of section for a LOD. */
	ENGINE_API int32 GetSectionNumber(int32 LODIndex) const;

	/** Return true if the section exist, false otherwise. */
	ENGINE_API bool IsValidSection(int32 LODIndex, int32 SectionIndex) const;

	/** Gets per-section settings for the specified LOD + section. */
	ENGINE_API FMeshSectionInfo Get(int32 LODIndex, int32 SectionIndex) const;

	/** Sets per-section settings for the specified LOD + section. */
	ENGINE_API void Set(int32 LODIndex, int32 SectionIndex, FMeshSectionInfo Info);

	/** Resets per-section settings for the specified LOD + section to defaults. */
	ENGINE_API void Remove(int32 LODIndex, int32 SectionIndex);

	/** Copies per-section settings from the specified section info map. */
	ENGINE_API void CopyFrom(const FMeshSectionInfoMap& Other);

	/** Returns true if any section of the specified LOD has collision enabled. */
	bool AnySectionHasCollision(int32 LodIndex) const;
};

USTRUCT()
struct FAssetEditorOrbitCameraPosition
{
	GENERATED_USTRUCT_BODY()

	FAssetEditorOrbitCameraPosition()
		: bIsSet(false)
		, CamOrbitPoint(ForceInitToZero)
		, CamOrbitZoom(ForceInitToZero)
		, CamOrbitRotation(ForceInitToZero)
	{
	}

	FAssetEditorOrbitCameraPosition(const FVector& InCamOrbitPoint, const FVector& InCamOrbitZoom, const FRotator& InCamOrbitRotation)
		: bIsSet(true)
		, CamOrbitPoint(InCamOrbitPoint)
		, CamOrbitZoom(InCamOrbitZoom)
		, CamOrbitRotation(InCamOrbitRotation)
	{
	}

	/** Whether or not this has been set to a valid value */
	UPROPERTY()
	bool bIsSet;

	/** The position to orbit the camera around */
	UPROPERTY()
	FVector	CamOrbitPoint;

	/** The distance of the camera from the orbit point */
	UPROPERTY()
	FVector CamOrbitZoom;

	/** The rotation to apply around the orbit point */
	UPROPERTY()
	FRotator CamOrbitRotation;
};

#if WITH_EDITOR
/** delegate type for pre mesh build events */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPreMeshBuild, class UStaticMesh*);
/** delegate type for post mesh build events */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostMeshBuild, class UStaticMesh*);
#endif

//~ Begin Material Interface for UStaticMesh - contains a material and other stuff
USTRUCT(BlueprintType)
struct FStaticMaterial
{
	GENERATED_USTRUCT_BODY()

		FStaticMaterial()
		: MaterialInterface(NULL)
		, MaterialSlotName(NAME_None)
#if WITH_EDITORONLY_DATA
		, ImportedMaterialSlotName(NAME_None)
#endif //WITH_EDITORONLY_DATA
	{

	}

	FStaticMaterial(class UMaterialInterface* InMaterialInterface
		, FName InMaterialSlotName = NAME_None
#if WITH_EDITORONLY_DATA
		, FName InImportedMaterialSlotName = NAME_None)
#else
		)
#endif
		: MaterialInterface(InMaterialInterface)
		, MaterialSlotName(InMaterialSlotName)
#if WITH_EDITORONLY_DATA
		, ImportedMaterialSlotName(InImportedMaterialSlotName)
#endif //WITH_EDITORONLY_DATA
	{
		//If not specified add some valid material slot name
		if (MaterialInterface && MaterialSlotName == NAME_None)
		{
			MaterialSlotName = MaterialInterface->GetFName();
		}
#if WITH_EDITORONLY_DATA
		if (ImportedMaterialSlotName == NAME_None)
		{
			ImportedMaterialSlotName = MaterialSlotName;
		}
#endif
	}

	friend FArchive& operator<<(FArchive& Ar, FStaticMaterial& Elem);

	ENGINE_API friend bool operator==(const FStaticMaterial& LHS, const FStaticMaterial& RHS);
	ENGINE_API friend bool operator==(const FStaticMaterial& LHS, const UMaterialInterface& RHS);
	ENGINE_API friend bool operator==(const UMaterialInterface& LHS, const FStaticMaterial& RHS);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StaticMesh)
	class UMaterialInterface* MaterialInterface;

	/*This name should be use by the gameplay to avoid error if the skeletal mesh Materials array topology change*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StaticMesh)
	FName MaterialSlotName;

	/*This name should be use when we re-import a skeletal mesh so we can order the Materials array like it should be*/
	UPROPERTY(VisibleAnywhere, Category = StaticMesh)
	FName ImportedMaterialSlotName;

	/** Data used for texture streaming relative to each UV channels. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = StaticMesh)
	FMeshUVChannelInfo			UVChannelData;
};


enum EImportStaticMeshVersion
{
	// Before any version changes were made
	BeforeImportStaticMeshVersionWasAdded,
	// Remove the material re-order workflow
	RemoveStaticMeshSkinxxWorkflow,
	StaticMeshVersionPlusOne,
	LastVersion = StaticMeshVersionPlusOne - 1
};

USTRUCT()
struct FMaterialRemapIndex
{
	GENERATED_USTRUCT_BODY()

	FMaterialRemapIndex()
	{
		ImportVersionKey = 0;
	}

	FMaterialRemapIndex(uint32 VersionKey, TArray<int32> RemapArray)
	: ImportVersionKey(VersionKey)
	, MaterialRemap(RemapArray)
	{
	}

	UPROPERTY()
	uint32 ImportVersionKey;

	UPROPERTY()
	TArray<int32> MaterialRemap;
};


/**
 * A StaticMesh is a piece of geometry that consists of a static set of polygons.
 * Static Meshes can be translated, rotated, and scaled, but they cannot have their vertices animated in any way. As such, they are more efficient
 * to render than other types of geometry such as USkeletalMesh, and they are often the basic building block of levels created in the engine.
 *
 * @see https://docs.unrealengine.com/latest/INT/Engine/Content/Types/StaticMeshes/
 * @see AStaticMeshActor, UStaticMeshComponent
 */
UCLASS(hidecategories=Object, customconstructor, MinimalAPI, BlueprintType, config=Engine)
class UStaticMesh : public UStreamableRenderAsset, public IInterface_CollisionDataProvider, public IInterface_AssetUserData, public IInterface_AsyncCompilation
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	/** Notification when bounds changed */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnExtendedBoundsChanged, const FBoxSphereBounds&);

	/** Notification when anything changed */
	DECLARE_MULTICAST_DELEGATE(FOnMeshChanged);
#endif
public:
	ENGINE_API ~UStaticMesh();

private:

#if WITH_EDITOR
	/** Used as a bit-field indicating which properties are currently accessed/modified by async compilation. */
	std::atomic<uint32> LockedProperties;

	void AcquireAsyncProperty(EStaticMeshAsyncProperties AsyncProperties = EStaticMeshAsyncProperties::All);
	void ReleaseAsyncProperty(EStaticMeshAsyncProperties AsyncProperties = EStaticMeshAsyncProperties::All);
	ENGINE_API void WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties AsyncProperties) const;
#else
	FORCEINLINE void AcquireAsyncProperty(EStaticMeshAsyncProperties AsyncProperties = EStaticMeshAsyncProperties::All) {};
	FORCEINLINE void ReleaseAsyncProperty(EStaticMeshAsyncProperties AsyncProperties = EStaticMeshAsyncProperties::All) {};
	FORCEINLINE void WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties AsyncProperties) const {}
#endif

	/** Pointer to the data used to render this static mesh. */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	TUniquePtr<class FStaticMeshRenderData> RenderData;

	/** Pointer to the occluder data used to rasterize this static mesh for software occlusion. */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	TUniquePtr<class FStaticMeshOccluderData> OccluderData;

public:
#if WITH_EDITOR
	ENGINE_API bool IsCompiling() const override { return AsyncTask != nullptr || LockedProperties.load(std::memory_order_relaxed) != 0; }
#else
	FORCEINLINE bool IsCompiling() const { return false; }
#endif
	
	ENGINE_API FStaticMeshRenderData* GetRenderData();
	ENGINE_API const FStaticMeshRenderData* GetRenderData() const;
	ENGINE_API void SetRenderData(TUniquePtr<class FStaticMeshRenderData>&& InRenderData);

	ENGINE_API FStaticMeshOccluderData* GetOccluderData();
	ENGINE_API const FStaticMeshOccluderData* GetOccluderData() const;
	ENGINE_API void SetOccluderData(TUniquePtr<class FStaticMeshOccluderData>&& InOccluderData);

#if WITH_EDITORONLY_DATA
	static const float MinimumAutoLODPixelError;

private:
	/** Imported raw mesh bulk data. */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY()
	TArray<FStaticMeshSourceModel> SourceModels;

	void SetLightmapUVVersion(int32 InLightmapUVVersion)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightmapUVVersion);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LightmapUVVersion = InLightmapUVVersion;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/** Map of LOD+Section index to per-section info. */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY()
	FMeshSectionInfoMap SectionInfoMap;

	/**
	 * We need the OriginalSectionInfoMap to be able to build mesh in a non destructive way. Reduce has to play with SectionInfoMap in case some sections disappear.
	 * This member will be update in the following situation
	 * 1. After a static mesh import/reimport
	 * 2. Postload, if the OriginalSectionInfoMap is empty, we will fill it with the current SectionInfoMap
	 *
	 * We do not update it when the user shuffle section in the staticmesh editor because the OriginalSectionInfoMap must always be in sync with the saved rawMesh bulk data.
	 */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY()
	FMeshSectionInfoMap OriginalSectionInfoMap;

public:
	static FName GetSectionInfoMapName()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GET_MEMBER_NAME_CHECKED(UStaticMesh, SectionInfoMap);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/** The LOD group to which this mesh belongs. */
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category=LodSettings)
	FName LODGroup;

	/**
	 * If non-negative, specify the maximum number of streamed LODs. Only has effect if
	 * mesh LOD streaming is enabled for the target platform.
	 */
	UPROPERTY()
	FPerPlatformInt NumStreamedLODs;

	/* The last import version */
	UPROPERTY()
	int32 ImportVersion;

	UPROPERTY()
	TArray<FMaterialRemapIndex> MaterialRemapIndexPerImportVersion;

private:
	/* The lightmap UV generation version used during the last derived data build */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY()
	int32 LightmapUVVersion;
public:

	int32 GetLightmapUVVersion() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightmapUVVersion);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return LightmapUVVersion;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/** If true, the screen sizees at which LODs swap are computed automatically. */
	UPROPERTY()
	uint8 bAutoComputeLODScreenSize : 1;

	/**
	* If true on post load we need to calculate Display Factors from the
	* loaded LOD distances.
	*/
	uint8 bRequiresLODDistanceConversion : 1;

	/**
	 * If true on post load we need to calculate resolution independent Display Factors from the
	 * loaded LOD screen sizes.
	 */
	uint8 bRequiresLODScreenSizeConversion : 1;

	/** Materials used by this static mesh. Individual sections index in to this array. */
	UPROPERTY()
	TArray<UMaterialInterface*> Materials_DEPRECATED;

	/** Settings related to building Nanite data. */
	UPROPERTY()
	FMeshNaniteSettings NaniteSettings;

#endif // #if WITH_EDITORONLY_DATA

	/** Minimum LOD to use for rendering.  This is the default setting for the mesh and can be overridden by component settings. */
	UE_DEPRECATED(4.27, "Please do not access this member directly; use UStaticMesh::GetMinLOD() or UStaticMesh::SetMinLOD().")
	UPROPERTY()
	FPerPlatformInt MinLOD;

	const FPerPlatformInt& GetMinLOD() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::MinLOD);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return MinLOD;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void SetMinLOD(FPerPlatformInt InMinLOD)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::MinLOD);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		MinLOD = MoveTemp(InMinLOD);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	UFUNCTION(BlueprintPure, Category=StaticMesh)
	void GetMinimumLODForPlatforms(TMap<FName, int32>& PlatformMinimumLODs) const
	{
#if WITH_EDITORONLY_DATA
		PlatformMinimumLODs = GetMinLOD().PerPlatform;
#endif
	}

	UFUNCTION(BlueprintPure, Category=StaticMesh)
	int32 GetMinimumLODForPlatform(const FName& PlatformName) const
	{
#if WITH_EDITORONLY_DATA
		if (const int32* Result = GetMinLOD().PerPlatform.Find(PlatformName))
		{
			return *Result;
		}
#endif
		return INDEX_NONE;
	}

private:
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY(BlueprintGetter = GetStaticMaterials, BlueprintSetter = SetStaticMaterials, Category = StaticMesh)
	TArray<FStaticMaterial> StaticMaterials;

public:
	static FName GetStaticMaterialsName()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GET_MEMBER_NAME_CHECKED(UStaticMesh, StaticMaterials);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	TArray<FStaticMaterial>& GetStaticMaterials()
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::StaticMaterials);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return StaticMaterials;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	UFUNCTION(BlueprintGetter)
	const TArray<FStaticMaterial>& GetStaticMaterials() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::StaticMaterials);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return StaticMaterials;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	UFUNCTION(BlueprintSetter)
	void SetStaticMaterials(const TArray<FStaticMaterial>& InStaticMaterials)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::StaticMaterials);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		StaticMaterials = InStaticMaterials;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

private:
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY()
	float LightmapUVDensity;
public:
	void SetLightmapUVDensity(float InLightmapUVDensity)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightmapUVDensity);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LightmapUVDensity = InLightmapUVDensity;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	float GetLightmapUVDensity() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightmapUVDensity);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return LightmapUVDensity;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	UE_DEPRECATED(4.27, "Please do not access this member directly; use UStaticMesh::GetLightMapResolution() or UStaticMesh::SetLightMapResolution().")
	UPROPERTY(EditAnywhere, Category=StaticMesh, meta=(ClampMax = 4096, ToolTip="The light map resolution", FixedIncrement="4.0"))
	int32 LightMapResolution;

	int32 GetLightMapResolution() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightMapResolution);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return LightMapResolution;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void SetLightMapResolution(int32 InLightMapResolution)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightMapResolution);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LightMapResolution = InLightMapResolution;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	static FName GetLightMapResolutionName()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GET_MEMBER_NAME_CHECKED(UStaticMesh, LightMapResolution);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/** The light map coordinate index */
	UE_DEPRECATED(4.27, "Please do not access this member directly; use UStaticMesh::GetLightMapCoordinateIndex() or UStaticMesh::SetLightMapCoordinateIndex().")
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=StaticMesh, meta=(ToolTip="The light map coordinate index", UIMin = "0", UIMax = "3"))
	int32 LightMapCoordinateIndex;

	int32 GetLightMapCoordinateIndex() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightMapCoordinateIndex);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return LightMapCoordinateIndex;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void SetLightMapCoordinateIndex(int32 InLightMapCoordinateIndex)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightMapCoordinateIndex);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LightMapCoordinateIndex = InLightMapCoordinateIndex;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	static FName GetLightMapCoordinateIndexName()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GET_MEMBER_NAME_CHECKED(UStaticMesh, LightMapCoordinateIndex);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/** Useful for reducing self shadowing from distance field methods when using world position offset to animate the mesh's vertices. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	float DistanceFieldSelfShadowBias;

private:
	// Physics data.
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY(EditAnywhere, transient, duplicatetransient, Instanced, Category = StaticMesh)
	class UBodySetup* BodySetup;
public:
	UBodySetup* GetBodySetup() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::BodySetup);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return BodySetup;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void SetBodySetup(UBodySetup* InBodySetup)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::BodySetup);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		BodySetup = InBodySetup;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	static FName GetBodySetupName()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GET_MEMBER_NAME_CHECKED(UStaticMesh, BodySetup);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/** 
	 *	Specifies which mesh LOD to use for complex (per-poly) collision. 
	 *	Sometimes it can be desirable to use a lower poly representation for collision to reduce memory usage, improve performance and behaviour.
	 *	Collision representation does not change based on distance to camera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StaticMesh, meta=(DisplayName="LOD For Collision"))
	int32 LODForCollision;

	/** 
	 * Whether to generate a distance field for this mesh, which can be used by DistanceField Indirect Shadows.
	 * This is ignored if the project's 'Generate Mesh Distance Fields' setting is enabled.
	 */
	UPROPERTY(EditAnywhere, Category=StaticMesh)
	uint8 bGenerateMeshDistanceField : 1;

	/** If true, strips unwanted complex collision data aka kDOP tree when cooking for consoles.
		On the Playstation 3 data of this mesh will be stored in video memory. */
	UPROPERTY()
	uint8 bStripComplexCollisionForConsole_DEPRECATED:1;

	/** If true, mesh will have NavCollision property with additional data for navmesh generation and usage.
	    Set to false for distant meshes (always outside navigation bounds) to save memory on collision data. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=Navigation)
	uint8 bHasNavigationData:1;

	/**	
		Mesh supports uniformly distributed sampling in constant time.
		Memory cost is 8 bytes per triangle.
		Example usage is uniform spawning of particles.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	uint8 bSupportUniformlyDistributedSampling : 1;

	/** 
		If true, complex collision data will store UVs and face remap table for use when performing
	    PhysicalMaterialMask lookups in cooked builds. Note the increased memory cost for this
		functionality.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	uint8 bSupportPhysicalMaterialMasks : 1;

private:
	/**
	 * If true, StaticMesh has been built at runtime
	 */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY()
	uint8 bIsBuiltAtRuntime : 1;
public:

	bool IsBuiltAtRuntime() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::IsBuiltAtRuntime);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return bIsBuiltAtRuntime;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void SetIsBuiltAtRuntime(bool InIsBuiltAtRuntime)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::IsBuiltAtRuntime);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		bIsBuiltAtRuntime = InIsBuiltAtRuntime;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
protected:
	/** Tracks whether InitResources has been called, and rendering resources are initialized. */
	uint8 bRenderingResourcesInitialized:1;

public:
	/** 
	 *	If true, will keep geometry data CPU-accessible in cooked builds, rather than uploading to GPU memory and releasing it from CPU memory.
	 *	This is required if you wish to access StaticMesh geometry data on the CPU at runtime in cooked builds (e.g. to convert StaticMesh to ProceduralMeshComponent)
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	uint8 bAllowCPUAccess:1;

	/**
	 * If true, a GPU buffer containing required data for uniform mesh surface sampling will be created at load time.
	 * It is created from the cpu data so bSupportUniformlyDistributedSampling is also required to be true.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = StaticMesh)
	uint8 bSupportGpuUniformlyDistributedSampling : 1;

	/** A fence which is used to keep track of the rendering thread releasing the static mesh resources. */
	FRenderCommandFence ReleaseResourcesFence;

#if WITH_EDITORONLY_DATA
	/** Importing data and options used for this mesh */
	UPROPERTY(EditAnywhere, Instanced, Category=ImportSettings)
	class UAssetImportData* AssetImportData;

	/** Path to the resource used to construct this static mesh */
	UPROPERTY()
	FString SourceFilePath_DEPRECATED;

	/** Date/Time-stamp of the file from the last import */
	UPROPERTY()
	FString SourceFileTimestamp_DEPRECATED;

	/** Information for thumbnail rendering */
	UPROPERTY(VisibleAnywhere, Instanced, AdvancedDisplay, Category=StaticMesh)
	class UThumbnailInfo* ThumbnailInfo;

	/** The stored camera position to use as a default for the static mesh editor */
	UPROPERTY()
	FAssetEditorOrbitCameraPosition EditorCameraPosition;

	/** If the user has modified collision in any way or has custom collision imported. Used for determining if to auto generate collision on import */
	UPROPERTY(EditAnywhere, Category = Collision)
	bool bCustomizedCollision;

	/** 
	 *	Specifies which mesh LOD to use as occluder geometry for software occlusion
	 *  Set to -1 to not use this mesh as occluder 
	 */
	UPROPERTY(EditAnywhere, Category=StaticMesh, AdvancedDisplay, meta=(DisplayName="LOD For Occluder Mesh"))
	int32 LODForOccluderMesh;

#endif // WITH_EDITORONLY_DATA

private:
	/** Unique ID for tracking/caching this mesh during distributed lighting */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	FGuid LightingGuid;
public:

	const FGuid& GetLightingGuid() const
	{
#if WITH_EDITORONLY_DATA
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightingGuid);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return LightingGuid;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
		static const FGuid NullGuid( 0, 0, 0, 0 );
		return NullGuid;
#endif // WITH_EDITORONLY_DATA
	}

	void SetLightingGuid(const FGuid& InLightingGuid = FGuid::NewGuid())
	{
#if WITH_EDITORONLY_DATA
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::LightingGuid);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LightingGuid = InLightingGuid;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif // WITH_EDITORONLY_DATA
	}

	/**
	 *	Array of named socket locations, set up in editor and used as a shortcut instead of specifying
	 *	everything explicitly to AttachComponent in the StaticMeshComponent.
	 */
	UPROPERTY()
	TArray<class UStaticMeshSocket*> Sockets;

	/** Data that is only available if this static mesh is an imported SpeedTree */
	TSharedPtr<class FSpeedTreeWind> SpeedTreeWind;

	/** Bound extension values in the positive direction of XYZ, positive value increases bound size */
	UE_DEPRECATED(4.27, "Please do not access this member directly; use UStaticMesh::GetPositiveBoundsExtension() or UStaticMesh::SetPositiveBoundsExtension.")
	UPROPERTY(EditDefaultsOnly, AdvancedDisplay, Category = StaticMesh)
	FVector PositiveBoundsExtension;

	const FVector& GetPositiveBoundsExtension() const
	{
		// No need for WaitUntilAsyncPropertyReleased here as this is only read during async Build/Postload
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return PositiveBoundsExtension;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void SetPositiveBoundsExtension(FVector InPositiveBoundsExtension)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::PositiveBoundsExtension);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		PositiveBoundsExtension = InPositiveBoundsExtension;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	static FName GetPositiveBoundsExtensionName()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GET_MEMBER_NAME_CHECKED(UStaticMesh, PositiveBoundsExtension);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/** Bound extension values in the negative direction of XYZ, positive value increases bound size */
	UE_DEPRECATED(4.27, "Please do not access this member directly; use UStaticMesh::GetNegativeBoundsExtension() or UStaticMesh::SetNegativeBoundsExtension.")
	UPROPERTY(EditDefaultsOnly, AdvancedDisplay, Category = StaticMesh)
	FVector NegativeBoundsExtension;
	
	const FVector& GetNegativeBoundsExtension() const
	{
		// No need for WaitUntilAsyncPropertyReleased here as this is not modified during async Build/Postload
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return NegativeBoundsExtension;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void SetNegativeBoundsExtension(FVector InNegativeBoundsExtension)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::NegativeBoundsExtension);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		NegativeBoundsExtension = InNegativeBoundsExtension;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	static FName GetNegativeBoundsExtensionName()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GET_MEMBER_NAME_CHECKED(UStaticMesh, NegativeBoundsExtension);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/** Original mesh bounds extended with Positive/NegativeBoundsExtension */
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY()
	FBoxSphereBounds ExtendedBounds;
public:

	const FBoxSphereBounds& GetExtendedBounds() const
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::ExtendedBounds);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return ExtendedBounds;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void SetExtendedBounds(const FBoxSphereBounds& InExtendedBounds)
	{
		WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::ExtendedBounds);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ExtendedBounds = InExtendedBounds;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if WITH_EDITOR
		OnExtendedBoundsChanged.Broadcast(InExtendedBounds);
#endif
	}

#if WITH_EDITOR
	FOnExtendedBoundsChanged OnExtendedBoundsChanged;
	FOnMeshChanged OnMeshChanged;

	/** This transient guid is use by the automation framework to modify the DDC key to force a build. */
	FGuid BuildCacheAutomationTestGuid;
#endif

protected:
	/**
	 * Index of an element to ignore while gathering streaming texture factors.
	 * This is useful to disregard automatically generated vertex data which breaks texture factor heuristics.
	 */
	UPROPERTY()
	int32 ElementToIgnoreForTexFactor;

	/** Array of user data stored with the asset */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Instanced, Category = StaticMesh)
	TArray<UAssetUserData*> AssetUserData;

	friend class FStaticMeshCompilingManager;
	friend class FStaticMeshAsyncBuildWorker;
	friend struct FStaticMeshUpdateContext;
	friend class FStaticMeshUpdate;

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(Instanced)
	class UObject* EditableMesh_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = Collision)
	class UStaticMesh* ComplexCollisionMesh;
#endif

	/**
	 * Registers the mesh attributes required by the mesh description for a static mesh.
	 */
	UE_DEPRECATED(4.24, "Please use FStaticMeshAttributes::Register to do this.")
	ENGINE_API static void RegisterMeshAttributes( FMeshDescription& MeshDescription );

#if WITH_EDITORONLY_DATA
	/*
	 * Return the MeshDescription associate to the LODIndex. The mesh description can be created on the fly if it was null
	 * and there is a FRawMesh data for this LODIndex.
	 */
	ENGINE_API FMeshDescription* GetMeshDescription(int32 LodIndex) const;

	/* 
	 * Clone the MeshDescription associated to the LODIndex.
	 *
	 * This will make a copy of any pending mesh description that hasn't been committed or will deserialize
	 * from the bulkdata or rawmesh directly if no current working copy exists.
	 */
	ENGINE_API bool CloneMeshDescription(int32 LodIndex, FMeshDescription& OutMeshDescription) const;

	ENGINE_API bool IsMeshDescriptionValid(int32 LodIndex) const;
	ENGINE_API FMeshDescription* CreateMeshDescription(int32 LodIndex);
	ENGINE_API FMeshDescription* CreateMeshDescription(int32 LodIndex, FMeshDescription MeshDescription);

	/** Structure that defines parameters passed into the commit mesh description function */
	struct FCommitMeshDescriptionParams
	{
		FCommitMeshDescriptionParams()
			: bMarkPackageDirty(true)
			, bUseHashAsGuid(false)
		{}

		/**
		 * If set to false, the caller can be from any thread but will have the
		 * responsability to call MarkPackageDirty() from the main thread.
		 */
		bool bMarkPackageDirty;

		/**
		 * Uses a hash as the GUID, useful to prevent recomputing content already in cache.
		 */
		bool bUseHashAsGuid;
	};

	/*
	 * Serialize the mesh description into its more optimized form.
	 *
	 * @param	LodIndex	Index of the StaticMesh LOD.
	 * @param	Params		Different options to use when committing mesh description
	 */
	ENGINE_API void CommitMeshDescription(int32 LodIndex, const FCommitMeshDescriptionParams& Params = FCommitMeshDescriptionParams());

	ENGINE_API void ClearMeshDescription(int32 LodIndex);
	ENGINE_API void ClearMeshDescriptions();

	/**
	 * Adds an empty UV channel at the end of the existing channels on the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @return true if a UV channel was added.
	 */
	ENGINE_API bool AddUVChannel(int32 LODIndex);

	/**
	 * Inserts an empty UV channel at the specified channel index on the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @param	UVChannelIndex		Index where to insert the UV channel.
	 * @return true if a UV channel was added.
	 */
	ENGINE_API bool InsertUVChannel(int32 LODIndex, int32 UVChannelIndex);

	/**
	 * Removes the UV channel at the specified channel index on the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @param	UVChannelIndex		Index where to remove the UV channel.
	 * @return true if the UV channel was removed.
	 */
	ENGINE_API bool RemoveUVChannel(int32 LODIndex, int32 UVChannelIndex);

	/**
	 * Sets the texture coordinates at the specified UV channel index on the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @param	UVChannelIndex		Index where to remove the UV channel.
	 * @param	TexCoords			The texture coordinates to set on the UV channel.
	 * @return true if the UV channel could be set.
	 */
	ENGINE_API bool SetUVChannel(int32 LODIndex, int32 UVChannelIndex, const TMap<FVertexInstanceID, FVector2D>& TexCoords);

#endif

	/** Create an empty StaticMeshDescription object, to describe a static mesh at runtime */
	UFUNCTION(BlueprintCallable, Category="StaticMesh")
	static ENGINE_API UStaticMeshDescription* CreateStaticMeshDescription(UObject* Outer = nullptr);

	/** Builds static mesh LODs from the array of StaticMeshDescriptions passed in */
	UFUNCTION(BlueprintCallable, Category="StaticMesh")
	ENGINE_API void BuildFromStaticMeshDescriptions(const TArray<UStaticMeshDescription*>& StaticMeshDescriptions, bool bBuildSimpleCollision = false);

	/** Return a new StaticMeshDescription referencing the MeshDescription of the given LOD */
	UFUNCTION(BlueprintCallable, Category="StaticMesh")
	ENGINE_API UStaticMeshDescription* GetStaticMeshDescription(int32 LODIndex);

	 /** Structure that defines parameters passed into the build mesh description function */
	struct FBuildMeshDescriptionsParams
	{
		FBuildMeshDescriptionsParams()
			: bMarkPackageDirty(true)
			, bUseHashAsGuid(false)
			, bBuildSimpleCollision(false)
			, bCommitMeshDescription(true)
		{}

		/**
		 * If set to false, the caller can be from any thread but will have the
		 * responsibility to call MarkPackageDirty() from the main thread.
		 */
		bool bMarkPackageDirty;

		/**
		 * Uses a hash as the GUID, useful to prevent recomputing content already in cache.
		 * Set to false by default.
		 */
		bool bUseHashAsGuid;

		/**
		 * Builds simple collision as part of the building process. Set to false by default.
		 */
		bool bBuildSimpleCollision;
	
		/**
		 * Commits the MeshDescription as part of the building process. Set to true by default.
		 */
		bool bCommitMeshDescription;
	};

	/**
	 * Builds static mesh render buffers from a list of MeshDescriptions, one per LOD.
	 */
	ENGINE_API bool BuildFromMeshDescriptions(const TArray<const FMeshDescription*>& MeshDescriptions, const FBuildMeshDescriptionsParams& Params = FBuildMeshDescriptionsParams());
	
	/** Builds a LOD resource from a MeshDescription */
	void BuildFromMeshDescription(const FMeshDescription& MeshDescription, FStaticMeshLODResources& LODResources);

	/**
	 * Returns the number of UV channels for the given LOD of a StaticMesh.
	 * @param	LODIndex			Index of the StaticMesh LOD.
	 * @return the number of UV channels.
	 */
	ENGINE_API int32 GetNumUVChannels(int32 LODIndex);

	/** Pre-build navigation collision */
private:
	UE_DEPRECATED(5.00, "This must be protected for async build, always use the accessors even internally.")
	UPROPERTY(VisibleAnywhere, transient, duplicatetransient, Instanced, Category = Navigation)
	UNavCollisionBase* NavCollision;

public:
	ENGINE_API void SetNavCollision(UNavCollisionBase*);
	ENGINE_API UNavCollisionBase* GetNavCollision() const;
	ENGINE_API bool IsNavigationRelevant() const;

	/**
	 * Default constructor
	 */
	ENGINE_API UStaticMesh(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ Begin UObject Interface.
#if WITH_EDITOR
	ENGINE_API virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	ENGINE_API virtual void PostEditUndo() override;
	ENGINE_API virtual void GetAssetRegistryTagMetadata(TMap<FName, FAssetRegistryTagMetadata>& OutMetadata) const override;
	ENGINE_API void SetLODGroup(FName NewGroup, bool bRebuildImmediately = true);
	ENGINE_API void BroadcastNavCollisionChange();

	FOnExtendedBoundsChanged& GetOnExtendedBoundsChanged() { return OnExtendedBoundsChanged; }
	FOnMeshChanged& GetOnMeshChanged() { return OnMeshChanged; }

	//SourceModels API
	ENGINE_API FStaticMeshSourceModel& AddSourceModel();

	UFUNCTION(BlueprintCallable, Category="StaticMesh")
	ENGINE_API void SetNumSourceModels(int32 Num);

	ENGINE_API void RemoveSourceModel(int32 Index);
	ENGINE_API TArray<FStaticMeshSourceModel>& GetSourceModels();
	ENGINE_API const TArray<FStaticMeshSourceModel>& GetSourceModels() const;
	ENGINE_API FStaticMeshSourceModel& GetSourceModel(int32 Index);
	ENGINE_API const FStaticMeshSourceModel& GetSourceModel(int32 Index) const;
	ENGINE_API int32 GetNumSourceModels() const;
	ENGINE_API bool IsSourceModelValid(int32 Index) const;

	ENGINE_API FMeshSectionInfoMap& GetSectionInfoMap();
	ENGINE_API const FMeshSectionInfoMap& GetSectionInfoMap() const;
	ENGINE_API FMeshSectionInfoMap& GetOriginalSectionInfoMap();
	ENGINE_API const FMeshSectionInfoMap& GetOriginalSectionInfoMap() const;

	/*
	 * Verify that a specific LOD using a material needing the adjacency buffer have the build option set to create the adjacency buffer.
	 *
	 * LODIndex: The LOD to fix
	 * bPreviewMode: If true the the function will not fix the build option. It will also change the return behavior, return true if the LOD need adjacency buffer, false otherwise
	 * bPromptUser: if true a dialog will ask the user if he agree changing the build option to allow adjacency buffer
	 * OutUserCancel: if the value is not null and the bPromptUser is true, the prompt dialog will have a cancel button and the result will be put in the parameter.
	 *
	 * The function will return true if any LOD build settings option is fix to add adjacency option. It will return false if no action was done. In case bPreviewMode is true it return true if the LOD need adjacency buffer, false otherwise.
	 */
	ENGINE_API bool FixLODRequiresAdjacencyInformation(const int32 LODIndex, const bool bPreviewMode = false, bool bPromptUser = false, bool* OutUserCancel = nullptr);
	ENGINE_API bool IsAsyncTaskComplete() const;
	
	/** Try to cancel any pending async tasks.
	 *  Returns true if there is no more async tasks pending, false otherwise.
	 */
	ENGINE_API bool TryCancelAsyncTasks();

	TUniquePtr<FStaticMeshAsyncBuildTask> AsyncTask;
#endif // WITH_EDITOR

	ENGINE_API virtual void Serialize(FArchive& Ar) override;
	ENGINE_API virtual void PostInitProperties() override;
	ENGINE_API virtual void PostLoad() override;
	ENGINE_API virtual bool IsPostLoadThreadSafe() const override;
	ENGINE_API virtual void BeginDestroy() override;
	ENGINE_API virtual bool IsReadyForFinishDestroy() override;
	ENGINE_API virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	ENGINE_API virtual FString GetDesc() override;
	ENGINE_API virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	ENGINE_API virtual bool CanBeClusterRoot() const override;
	//~ End UObject Interface.

	//~ Begin UStreamableRenderAsset Interface
	ENGINE_API virtual int32 CalcCumulativeLODSize(int32 NumLODs) const final override;
	ENGINE_API virtual FIoFilenameHash GetMipIoFilenameHash(const int32 MipIndex) const final override;
	ENGINE_API virtual bool DoesMipDataExist(const int32 MipIndex) const final override;
	ENGINE_API virtual bool HasPendingRenderResourceInitialization() const final override;
	ENGINE_API virtual bool StreamOut(int32 NewMipCount) final override;
	ENGINE_API virtual bool StreamIn(int32 NewMipCount, bool bHighPrio) final override;
	ENGINE_API virtual EStreamableRenderAssetType GetRenderAssetType() const final override { return EStreamableRenderAssetType::StaticMesh; }
	//~ End UStreamableRenderAsset Interface

#if USE_BULKDATA_STREAMING_TOKEN
	UE_DEPRECATED(5.0, "Use GetMipDataPackagePath instead")
	bool GetMipDataFilename(const int32 MipIndex, FString& OutBulkDataFilename) const;
	bool GetMipDataPackagePath(const int32 MipIndex, FPackagePath& OutPackagePath, EPackageSegment& OutPackageSegment) const;
#endif

	/**
	* Cancels any pending static mesh streaming actions if possible.
	* Returns when no more async loading requests are in flight.
	*/
	ENGINE_API static void CancelAllPendingStreamingActions();

	/**
	 * Rebuilds renderable data for this static mesh, automatically made async if enabled.
	 * @param		bInSilent	If true will not popup a progress dialog.
	 * @param [out]	OutErrors	If provided, will contain the errors that occurred during this process. This will prevent async static mesh compilation because OutErrors could get out of scope.
	 */
	ENGINE_API void Build(bool bInSilent = false, TArray<FText>* OutErrors = nullptr);

	/**
	 * Rebuilds renderable data for a batch of static meshes.
	 * @param		InStaticMeshes		The list of all static meshes to build.
	 * @param		bInSilent			If true will not popup a progress dialog.
	 * @param		InProgressCallback	If provided, will be used to abort task and report progress to higher level functions (should return true to continue, false to abort).
	 * @param [out]	OutErrors			If provided, will contain the errors that occurred during this process. This will prevent async static mesh compilation because OutErrors could get out of scope.
	 */
	ENGINE_API static void BatchBuild(const TArray<UStaticMesh*>& InStaticMeshes, bool bInSilent = false, TFunction<bool(UStaticMesh*)> InProgressCallback = nullptr, TArray<FText>* OutErrors = nullptr);

	/**
	 * Initialize the static mesh's render resources.
	 */
	ENGINE_API virtual void InitResources();

	/**
	 * Releases the static mesh's render resources.
	 */
	ENGINE_API virtual void ReleaseResources();

	/**
	 * Update missing material UV channel data used for texture streaming. 
	 *
	 * @param bRebuildAll		If true, rebuild everything and not only missing data.
	 */
	ENGINE_API void UpdateUVChannelData(bool bRebuildAll);

	/**
	 * Returns the material bounding box. Computed from all lod-section using the material index.
	 *
	 * @param MaterialIndex			Material Index to look at
	 * @param TransformMatrix		Matrix to be applied to the position before computing the bounds
	 *
	 * @return false if some parameters are invalid
	 */
	ENGINE_API FBox GetMaterialBox(int32 MaterialIndex, const FTransform& Transform) const;

	/**
	 * Returns the UV channel data for a given material index. Used by the texture streamer.
	 * This data applies to all lod-section using the same material.
	 *
	 * @param MaterialIndex		the material index for which to get the data for.
	 * @return the data, or null if none exists.
	 */
	ENGINE_API const FMeshUVChannelInfo* GetUVChannelData(int32 MaterialIndex) const;

	/**
	 * Returns the number of vertices for the specified LOD.
	 */
	ENGINE_API int32 GetNumVertices(int32 LODIndex) const;

	/**
	 * Returns the number of LODs used by the mesh.
	 */
	UFUNCTION(BlueprintPure, Category = "StaticMesh", meta=(ScriptName="GetNumLods"))
	ENGINE_API int32 GetNumLODs() const;

	/**
	 * Returns true if the mesh has data that can be rendered.
	 */
	ENGINE_API bool HasValidRenderData(bool bCheckLODForVerts = true, int32 LODIndex = INDEX_NONE) const;

	/**
	 * Returns the number of bounds of the mesh.
	 *
	 * @return	The bounding box represented as box origin with extents and also a sphere that encapsulates that box
	 */
	UFUNCTION( BlueprintPure, Category="StaticMesh" )
	ENGINE_API FBoxSphereBounds GetBounds() const;

	/** Returns the bounding box, in local space including bounds extension(s), of the StaticMesh asset */
	UFUNCTION(BlueprintPure, Category="StaticMesh")
	ENGINE_API FBox GetBoundingBox() const;

	/** Returns number of Sections that this StaticMesh has, in the supplied LOD (LOD 0 is the highest) */
	UFUNCTION(BlueprintPure, Category = "StaticMesh")
	ENGINE_API int32 GetNumSections(int32 InLOD) const;

	/**
	 * Gets a Material given a Material Index and an LOD number
	 *
	 * @return Requested material
	 */
	UFUNCTION(BlueprintPure, Category = "StaticMesh")
	ENGINE_API UMaterialInterface* GetMaterial(int32 MaterialIndex) const;

	/**
	 * Adds a new material and return its slot name
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API FName AddMaterial(UMaterialInterface* Material);

	/**
	 * Gets a Material index given a slot name
	 *
	 * @return Requested material
	 */
	UFUNCTION(BlueprintPure, Category = "StaticMesh")
	ENGINE_API int32 GetMaterialIndex(FName MaterialSlotName) const;

	ENGINE_API int32 GetMaterialIndexFromImportedMaterialSlotName(FName ImportedMaterialSlotName) const;

	/**
	 * Returns the render data to use for exporting the specified LOD. This method should always
	 * be called when exporting a static mesh.
	 */
	ENGINE_API const FStaticMeshLODResources& GetLODForExport(int32 LODIndex) const;

	/**
	 * Static: Processes the specified static mesh for light map UV problems
	 *
	 * @param	InStaticMesh					Static mesh to process
	 * @param	InOutAssetsWithMissingUVSets	Array of assets that we found with missing UV sets
	 * @param	InOutAssetsWithBadUVSets		Array of assets that we found with bad UV sets
	 * @param	InOutAssetsWithValidUVSets		Array of assets that we found with valid UV sets
	 * @param	bInVerbose						If true, log the items as they are found
	 */
	ENGINE_API static void CheckLightMapUVs( UStaticMesh* InStaticMesh, TArray< FString >& InOutAssetsWithMissingUVSets, TArray< FString >& InOutAssetsWithBadUVSets, TArray< FString >& InOutAssetsWithValidUVSets, bool bInVerbose = true );

	//~ Begin Interface_CollisionDataProvider Interface
	ENGINE_API virtual bool GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	ENGINE_API virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override;
private:
		bool GetPhysicsTriMeshDataCheckComplex(struct FTriMeshCollisionData* CollisionData, bool bInUseAllTriData, bool bInCheckComplexCollisionMesh);
		bool ContainsPhysicsTriMeshDataCheckComplex(bool InUseAllTriData, bool bInCheckComplexCollisionMesh) const;
public:

	virtual bool WantsNegXTriMesh() override
	{
		return true;
	}
	ENGINE_API virtual void GetMeshId(FString& OutMeshId) override;
	//~ End Interface_CollisionDataProvider Interface

	/** Return the number of sections of the StaticMesh with collision enabled */
	int32 GetNumSectionsWithCollision() const;

	//~ Begin IInterface_AssetUserData Interface
	ENGINE_API virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	ENGINE_API virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	ENGINE_API virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	ENGINE_API virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData Interface


	/**
	 * Create BodySetup for this staticmesh if it doesn't have one
	 */
	ENGINE_API void CreateBodySetup();

	/**
	 * Calculates navigation collision for caching
	 */
	ENGINE_API void CreateNavCollision(const bool bIsUpdate = false);

	/** Configures this SM as bHasNavigationData = false and clears stored NavCollision */
	ENGINE_API void MarkAsNotHavingNavigationData();


	/**
	 *	Add a socket object in this StaticMesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API void AddSocket(UStaticMeshSocket* Socket);

	/**
	 *	Find a socket object in this StaticMesh by name.
	 *	Entering NAME_None will return NULL. If there are multiple sockets with the same name, will return the first one.
	 */
	UFUNCTION(BlueprintPure, Category = "StaticMesh")
	ENGINE_API class UStaticMeshSocket* FindSocket(FName InSocketName) const;

	/**
	 *	Remove a socket object in this StaticMesh by providing it's pointer. Use FindSocket() if needed.
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API void RemoveSocket(UStaticMeshSocket* Socket);

	/**
	 * Returns vertex color data by position.
	 * For matching to reimported meshes that may have changed or copying vertex paint data from mesh to mesh.
	 *
	 *	@param	VertexColorData		(out)A map of vertex position data and its color. The method fills this map.
	 */
	ENGINE_API void GetVertexColorData(TMap<FVector, FColor>& VertexColorData);

	/**
	 * Sets vertex color data by position.
	 * Map of vertex color data by position is matched to the vertex position in the mesh
	 * and nearest matching vertex color is used.
	 *
	 *	@param	VertexColorData		A map of vertex position data and color.
	 */
	ENGINE_API void SetVertexColorData(const TMap<FVector, FColor>& VertexColorData);

	/** Removes all vertex colors from this mesh and rebuilds it (Editor only */
	ENGINE_API void RemoveVertexColors();

	/** Make sure the Lightmap UV point on a valid UVChannel */
	ENGINE_API void EnforceLightmapRestrictions(bool bUseRenderData = true);

	/** Calculates the extended bounds */
	ENGINE_API void CalculateExtendedBounds();

	inline bool AreRenderingResourcesInitialized() const { return bRenderingResourcesInitialized; }

#if WITH_EDITOR

	/**
	 * Sets a Material given a Material Index
	 */
	UFUNCTION(BlueprintCallable, Category = "StaticMesh")
	ENGINE_API void SetMaterial(int32 MaterialIndex, UMaterialInterface* NewMaterial);

	/**
	 * Returns true if LODs of this static mesh may share texture lightmaps.
	 */
	ENGINE_API bool CanLODsShareStaticLighting() const;

	/**
	 * Retrieves the names of all LOD groups.
	 */
	ENGINE_API static void GetLODGroups(TArray<FName>& OutLODGroups);

	/**
	 * Retrieves the localized display names of all LOD groups.
	 */
	ENGINE_API static void GetLODGroupsDisplayNames(TArray<FText>& OutLODGroupsDisplayNames);

	ENGINE_API void GenerateLodsInPackage();

	ENGINE_API virtual void PostDuplicate(bool bDuplicateForPIE) override;

	/** Get multicast delegate broadcast prior to mesh building */
	FOnPreMeshBuild& OnPreMeshBuild() { return PreMeshBuild; }

	/** Get multicast delegate broadcast after mesh building */
	FOnPostMeshBuild& OnPostMeshBuild() { return PostMeshBuild; }
	

	/* Return true if the reduction settings are setup to reduce a LOD*/
	ENGINE_API bool IsReductionActive(int32 LODIndex) const;

	/* Get a copy of the reduction settings for a specified LOD index. */
	ENGINE_API struct FMeshReductionSettings GetReductionSettings(int32 LODIndex) const;

private:
	/**
	 * Converts legacy LODDistance in the source models to Display Factor
	 */
	void ConvertLegacyLODDistance();

	/**
	 * Converts legacy LOD screen area in the source models to resolution-independent screen size
	 */
	void ConvertLegacyLODScreenArea();

	/**
	 * Fixes up static meshes that were imported with sections that had zero triangles.
	 */
	void FixupZeroTriangleSections();

	/**
	* Return mesh data key. The key is the ddc filename for the mesh data
	*/
	bool GetMeshDataKey(int32 LodIndex, FString& OutKey) const;

	/**
	* Caches mesh data.
	*/
	void CacheMeshData();
	
	/**
	 * Verify if the static mesh can be built.
	 */
	bool CanBuild() const;

	/**
	 * Initial step for the static mesh building process - Can't be done in parallel.
	 */
	void BeginBuildInternal(FStaticMeshBuildContext* Context = nullptr);

	/**
	 * Build the static mesh
	 */
	bool ExecuteBuildInternal(bool bSilent, TArray<FText>* OutErrors);

	/**
	 * Complete the static mesh building process - Can't be done in parallel.
	 */
	void FinishBuildInternal(const TArray<UStaticMeshComponent*>& InAffectedComponents, bool bHasRenderDataChanged, bool bShouldComputeExtendedBounds = true);

#if WITH_EDITORONLY_DATA
	/**
	 * Deserialize MeshDescription for the specified LodIndex from BulkData, DDC or RawMesh.
	 */
	bool LoadMeshDescription(int32 LodIndex, FMeshDescription& OutMeshDescription) const;
#endif

public:
	/**
	 * Caches derived renderable data.
	 */
	ENGINE_API void CacheDerivedData();

private:
	// Filled at CommitDescription time and reused during build
	TOptional<FBoxSphereBounds> CachedMeshDescriptionBounds;

	FOnPreMeshBuild PreMeshBuild;
	FOnPostMeshBuild PostMeshBuild;

	/**
	 * Fixes up the material when it was converted to the new staticmesh build process
	 */
	bool bCleanUpRedundantMaterialPostLoad;

	/**
	 * Guard to ignore re-entrant PostEditChange calls.
	 */
	bool bIsInPostEditChange = false;
#endif // #if WITH_EDITOR

	/**
	 * Initial step for the Post Load process - Can't be done in parallel.
	 */
	void BeginPostLoadInternal(FStaticMeshPostLoadContext& Context);

	/**
	 * Thread-safe part of the Post Load
	 */
	void ExecutePostLoadInternal(FStaticMeshPostLoadContext& Context);

	/**
	 * Complete the static mesh postload process - Can't be done in parallel.
	 */
	void FinishPostLoadInternal(FStaticMeshPostLoadContext& Context);
};

class FStaticMeshPostLoadContext
{
public:
	bool bShouldComputeExtendedBounds = false;
	bool bNeedsMeshUVDensityFix = false;
	bool bNeedsMaterialFixup = false;
	bool bIsCookedForEditor = false;
};

class FStaticMeshBuildContext
{
public:
	bool bHasRenderDataChanged = false;
	bool bShouldComputeExtendedBounds = true;
};