// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
InstancedFoliage.h: Instanced foliage type definitions.
=============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "FoliageInstanceBase.h"

class AInstancedFoliageActor;
class UActorComponent;
class UFoliageType;
class UHierarchicalInstancedStaticMeshComponent;
class UFoliageType_InstancedStaticMesh;
class UPrimitiveComponent;
class UStaticMesh;
struct FFoliageInstanceHash;

DECLARE_LOG_CATEGORY_EXTERN(LogInstancedFoliage, Log, All);

/**
* Flags stored with each instance
*/
enum EFoliageInstanceFlags
{
	FOLIAGE_AlignToNormal = 0x00000001,
	FOLIAGE_NoRandomYaw = 0x00000002,
	FOLIAGE_Readjusted = 0x00000004,
	FOLIAGE_InstanceDeleted = 0x00000008,	// Used only for migration from pre-HierarchicalISM foliage.
};

/**
*	FFoliageInstancePlacementInfo - placement info an individual instance
*/
struct FFoliageInstancePlacementInfo
{
	FVector Location;
	FRotator Rotation;
	FRotator PreAlignRotation;
	FVector DrawScale3D;
	float ZOffset;
	uint32 Flags;

	FFoliageInstancePlacementInfo()
		: Location(0.f, 0.f, 0.f)
		, Rotation(0, 0, 0)
		, PreAlignRotation(0, 0, 0)
		, DrawScale3D(1.f, 1.f, 1.f)
		, ZOffset(0.f)
		, Flags(0)
	{}
};

/**
*	Legacy instance
*/
struct FFoliageInstance_Deprecated : public FFoliageInstancePlacementInfo
{
	UActorComponent* Base;
	FGuid ProceduralGuid;
	friend FArchive& operator<<(FArchive& Ar, FFoliageInstance_Deprecated& Instance);
};

/**
*	FFoliageInstance - editor info an individual instance
*/
struct FFoliageInstance : public FFoliageInstancePlacementInfo
{
	// ID of base this instance was painted on
	FFoliageInstanceBaseId BaseId;

	FGuid ProceduralGuid;

	UActorComponent* BaseComponent;

	FFoliageInstance()
		: BaseId(0)
		, BaseComponent(nullptr)
	{}

	friend FArchive& operator<<(FArchive& Ar, FFoliageInstance& Instance);

	FTransform GetInstanceWorldTransform() const
	{
		return FTransform(Rotation, Location, DrawScale3D);
	}

	void AlignToNormal(const FVector& InNormal, float AlignMaxAngle = 0.f)
	{
		Flags |= FOLIAGE_AlignToNormal;

		FRotator AlignRotation = InNormal.Rotation();
		// Static meshes are authored along the vertical axis rather than the X axis, so we add 90 degrees to the static mesh's Pitch.
		AlignRotation.Pitch -= 90.f;
		// Clamp its value inside +/- one rotation
		AlignRotation.Pitch = FRotator::NormalizeAxis(AlignRotation.Pitch);

		// limit the maximum pitch angle if it's > 0.
		if (AlignMaxAngle > 0.f)
		{
			int32 MaxPitch = AlignMaxAngle;
			if (AlignRotation.Pitch > MaxPitch)
			{
				AlignRotation.Pitch = MaxPitch;
			}
			else if (AlignRotation.Pitch < -MaxPitch)
			{
				AlignRotation.Pitch = -MaxPitch;
			}
		}

		PreAlignRotation = Rotation;
		Rotation = FRotator(FQuat(AlignRotation) * FQuat(Rotation));
	}
};

struct FFoliageMeshInfo_Deprecated
{
	UHierarchicalInstancedStaticMeshComponent* Component;

#if WITH_EDITORONLY_DATA
	// Allows us to detect if FoliageType was updated while this level wasn't loaded
	FGuid FoliageTypeUpdateGuid;

	// Editor-only placed instances
	TArray<FFoliageInstance_Deprecated> Instances;
#endif

	FFoliageMeshInfo_Deprecated()
		: Component(nullptr)
	{
	}

	friend FArchive& operator<<(FArchive& Ar, FFoliageMeshInfo_Deprecated& MeshInfo);
};

/**
*	FFoliageInfo - editor info for all matching foliage meshes
*/
struct FFoliageMeshInfo_Deprecated2
{
	UHierarchicalInstancedStaticMeshComponent* Component;

#if WITH_EDITORONLY_DATA
	// Allows us to detect if FoliageType was updated while this level wasn't loaded
	FGuid FoliageTypeUpdateGuid;

	// Editor-only placed instances
	TArray<FFoliageInstance> Instances;
#endif


	FFoliageMeshInfo_Deprecated2();
	
	friend FArchive& operator<<(FArchive& Ar, FFoliageMeshInfo_Deprecated2& MeshInfo);
};

///
/// EFoliageImplType
///
enum class EFoliageImplType : uint8
{
	Unknown = 0,
	StaticMesh = 1,
	Actor = 2
};

///
/// FFoliageInfoImpl
///
struct FFoliageImpl
{
	FFoliageImpl() {}
	virtual ~FFoliageImpl() {}

	virtual void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector) {}
	virtual void Serialize(FArchive& Ar) = 0;

#if WITH_EDITOR
	virtual bool IsInitialized() const = 0;
	virtual void Initialize(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType) = 0;
	virtual void Uninitialize() = 0;
	virtual int32 GetInstanceCount() const = 0;
	virtual void PreAddInstances(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType, int32 Count) = 0;
	virtual void AddInstance(AInstancedFoliageActor* IFA, const FFoliageInstance& NewInstance) = 0;
	virtual void RemoveInstance(int32 InstanceIndex) = 0;
	virtual void MoveInstance(int32 InstanceIndex, UObject*& OutInstanceImplementation) { RemoveInstance(InstanceIndex); }
	virtual void AddExistingInstance(AInstancedFoliageActor* IFA, const FFoliageInstance& ExistingInstance, UObject* InstanceImplementation) { AddInstance(IFA, ExistingInstance); }
	virtual void SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport) = 0;
	virtual FTransform GetInstanceWorldTransform(int32 InstanceIndex) const = 0;
	virtual void PostUpdateInstances() {}
	virtual void PreMoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesMoved) {}
	virtual void PostMoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesMoved, bool bFinished) {}
	virtual bool IsOwnedComponent(const UPrimitiveComponent* Component) const = 0;
	virtual int32 FindIndex(const UPrimitiveComponent* HitComponent) const { return INDEX_NONE; }

	virtual void SelectAllInstances(bool bSelect) = 0;
	virtual void SelectInstance(bool bSelect, int32 Index) = 0;
	virtual void SelectInstances(bool bSelect, const TSet<int32>& SelectedIndices) = 0;
	virtual void ApplySelection(bool bApply, const TSet<int32>& SelectedIndices) = 0;
	virtual void ClearSelection(const TSet<int32>& SelectedIndices) = 0;

	virtual void BeginUpdate() {}
	virtual void EndUpdate() {}
	virtual void Refresh(AInstancedFoliageActor* IFA, const TArray<FFoliageInstance>& Instances, bool Async, bool Force) {}
	virtual void OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews) = 0;
	virtual void PreEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType) {}
	virtual void PostEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices) = 0;
	virtual void NotifyFoliageTypeWillChange(AInstancedFoliageActor* IFA, UFoliageType* FoliageType) {}
	virtual void NotifyFoliageTypeChanged(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, const TArray<FFoliageInstance>& Instances, const TSet<int32>& SelectedIndices, bool bSourceChanged) = 0;
	virtual void EnterEditMode() {}
	virtual void ExitEditMode() {}
	virtual bool ShouldAttachToBaseComponent() const { return true; }
#endif

	virtual int32 GetOverlappingSphereCount(const FSphere& Sphere) const { return 0; }
	virtual int32 GetOverlappingBoxCount(const FBox& Box) const { return 0; }
	virtual void GetOverlappingBoxTransforms(const FBox& Box, TArray<FTransform>& OutTransforms) const { }
	virtual void GetOverlappingMeshCount(const FSphere& Sphere, TMap<UStaticMesh*, int32>& OutCounts) const { }
};

///
/// FFoliageInfo
///
struct FFoliageInfo
{
	EFoliageImplType Type;
	TUniquePtr<FFoliageImpl> Implementation;

#if WITH_EDITORONLY_DATA
	// Allows us to detect if FoliageType was updated while this level wasn't loaded
	FGuid FoliageTypeUpdateGuid;

	// Editor-only placed instances
	TArray<FFoliageInstance> Instances;

	// Transient, editor-only locality hash of instances
	TUniquePtr<FFoliageInstanceHash> InstanceHash;

	// Transient, editor-only set of instances per component
	TMap<FFoliageInstanceBaseId, TSet<int32>> ComponentHash;

	// Transient, editor-only list of selected instances.
	TSet<int32> SelectedIndices;

	// Moving instances
	bool bMovingInstances;
#endif

	FOLIAGE_API FFoliageInfo();
	FOLIAGE_API ~FFoliageInfo();

	FFoliageInfo(FFoliageInfo&& Other) = default;
	FFoliageInfo& operator=(FFoliageInfo&& Other) = default;

	// Will only return a valid component in the case of non-actor foliage
	FOLIAGE_API UHierarchicalInstancedStaticMeshComponent* GetComponent() const;

	void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
		
	FOLIAGE_API void CreateImplementation(EFoliageImplType InType);
	FOLIAGE_API void Initialize(AInstancedFoliageActor* InIFA, const UFoliageType* FoliageType);
	FOLIAGE_API void Uninitialize();
	FOLIAGE_API bool IsInitialized() const;
		
	int32 GetOverlappingSphereCount(const FSphere& Sphere) const;
	int32 GetOverlappingBoxCount(const FBox& Box) const;
	void GetOverlappingBoxTransforms(const FBox& Box, TArray<FTransform>& OutTransforms) const;
	void GetOverlappingMeshCount(const FSphere& Sphere, TMap<UStaticMesh*, int32>& OutCounts) const;
	
#if WITH_EDITOR
	
	FOLIAGE_API void CreateImplementation(const UFoliageType* FoliageType);
	FOLIAGE_API void NotifyFoliageTypeWillChange(AInstancedFoliageActor* IFA, UFoliageType* FoliageType);
	FOLIAGE_API void NotifyFoliageTypeChanged(AInstancedFoliageActor* IFA, UFoliageType* FoliageType, bool bSourceChanged);
	
	FOLIAGE_API void ClearSelection();

	FOLIAGE_API void SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport);

	FOLIAGE_API void SetRandomSeed(int32 seed);
	FOLIAGE_API void AddInstance(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings, const FFoliageInstance& InNewInstance);
	FOLIAGE_API void AddInstance(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings, const FFoliageInstance& InNewInstance, UActorComponent* InBaseComponent);
	FOLIAGE_API void AddInstances(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings, const TArray<const FFoliageInstance*>& InNewInstances);

	FOLIAGE_API void RemoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesToRemove, bool RebuildFoliageTree);

	FOLIAGE_API void MoveInstances(AInstancedFoliageActor* InFromIFA, AInstancedFoliageActor* InToIFA, const TSet<int32>& InInstancesToMove, bool bKeepSelection);

	// Apply changes in the FoliageType to the component
	FOLIAGE_API void PreMoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesToMove);
	FOLIAGE_API void PostMoveInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesMoved, bool bFinished = false);
	FOLIAGE_API void PostUpdateInstances(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesUpdated, bool bReAddToHash = false, bool InUpdateSelection = false);
	FOLIAGE_API void DuplicateInstances(AInstancedFoliageActor* InIFA, UFoliageType* InSettings, const TArray<int32>& InInstancesToDuplicate);
	FOLIAGE_API void GetInstancesInsideSphere(const FSphere& Sphere, TArray<int32>& OutInstances);
	FOLIAGE_API void GetInstanceAtLocation(const FVector& Location, int32& OutInstance, bool& bOutSucess);
	FOLIAGE_API bool CheckForOverlappingSphere(const FSphere& Sphere);
	FOLIAGE_API bool CheckForOverlappingInstanceExcluding(int32 TestInstanceIdx, float Radius, TSet<int32>& ExcludeInstances);
	FOLIAGE_API TArray<int32> GetInstancesOverlappingBox(const FBox& Box) const;

	// Destroy existing clusters and reassign all instances to new clusters
	FOLIAGE_API void ReallocateClusters(AInstancedFoliageActor* InIFA, UFoliageType* InSettings);

	FOLIAGE_API void SelectInstances(AInstancedFoliageActor* InIFA, bool bSelect, TArray<int32>& Instances);

	FOLIAGE_API void SelectInstances(AInstancedFoliageActor* InIFA, bool bSelect);

	// Get the number of placed instances
	FOLIAGE_API int32 GetPlacedInstanceCount() const;

	FOLIAGE_API void AddToBaseHash(int32 InstanceIdx);
	FOLIAGE_API void RemoveFromBaseHash(int32 InstanceIdx);
	FOLIAGE_API bool ShouldAttachToBaseComponent() const { return Implementation->ShouldAttachToBaseComponent(); }

	// For debugging. Validate state after editing.
	void CheckValid();
		
	FOLIAGE_API void Refresh(AInstancedFoliageActor* IFA, bool Async, bool Force);
	FOLIAGE_API void OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews);
	FOLIAGE_API void PostEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType);
	FOLIAGE_API void PreEditUndo(AInstancedFoliageActor* IFA, UFoliageType* FoliageType);
	FOLIAGE_API void EnterEditMode();
	FOLIAGE_API void ExitEditMode();

	FOLIAGE_API void RemoveBaseComponentOnInstances();
	FOLIAGE_API void IncludeActor(AInstancedFoliageActor* IFA, const UFoliageType* FoliageType, AActor* InActor);
	FOLIAGE_API void ExcludeActors();
#endif

	friend FArchive& operator<<(FArchive& Ar, FFoliageInfo& MeshInfo);

	// Non-copyable
	FFoliageInfo(const FFoliageInfo&) = delete;
	FFoliageInfo& operator=(const FFoliageInfo&) = delete;

private:
	using FAddImplementationFunc = TFunctionRef<void(FFoliageImpl*, AInstancedFoliageActor*, const FFoliageInstance&)>;
	void AddInstancesImpl(AInstancedFoliageActor* InIFA, const UFoliageType* InSettings, const TArray<const FFoliageInstance*>& InNewInstances, FFoliageInfo::FAddImplementationFunc ImplementationFunc);
	void AddInstanceImpl(AInstancedFoliageActor* InIFA, const FFoliageInstance& InNewInstance, FAddImplementationFunc ImplementationFunc);

	using FRemoveImplementationFunc = TFunctionRef<void(FFoliageImpl*, int32)>;
	void RemoveInstancesImpl(AInstancedFoliageActor* InIFA, const TArray<int32>& InInstancesToRemove, bool RebuildFoliageTree, FRemoveImplementationFunc ImplementationFunc);
};


#if WITH_EDITORONLY_DATA
//
// FFoliageInstanceHash
//

#define FOLIAGE_HASH_CELL_BITS 9	// 512x512 grid

struct FFoliageInstanceHash
{
private:
	const int32 HashCellBits;
	TMap<uint64, TSet<int32>> CellMap;

	uint64 MakeKey(int32 CellX, int32 CellY) const
	{
		return ((uint64)(*(uint32*)(&CellX)) << 32) | (*(uint32*)(&CellY) & 0xffffffff);
	}

	uint64 MakeKey(const FVector& Location) const
	{
		return  MakeKey(FMath::FloorToInt(Location.X) >> HashCellBits, FMath::FloorToInt(Location.Y) >> HashCellBits);
	}

public:
	FFoliageInstanceHash(int32 InHashCellBits = FOLIAGE_HASH_CELL_BITS)
		: HashCellBits(InHashCellBits)
	{}

	void InsertInstance(const FVector& InstanceLocation, int32 InstanceIndex)
	{
		uint64 Key = MakeKey(InstanceLocation);

		CellMap.FindOrAdd(Key).Add(InstanceIndex);
	}

	void RemoveInstance(const FVector& InstanceLocation, int32 InstanceIndex, bool bChecked = true)
	{
		uint64 Key = MakeKey(InstanceLocation);

		if (bChecked)
		{
			int32 RemoveCount = CellMap.FindChecked(Key).Remove(InstanceIndex);
			check(RemoveCount == 1);
		}
		else if(TSet<int32>* Value = CellMap.Find(Key))
		{
			Value->Remove(InstanceIndex);
		}
	}

	void GetInstancesOverlappingBox(const FBox& InBox, TArray<int32>& OutInstanceIndices) const
	{
		int32 MinX = FMath::FloorToInt(InBox.Min.X) >> HashCellBits;
		int32 MinY = FMath::FloorToInt(InBox.Min.Y) >> HashCellBits;
		int32 MaxX = FMath::FloorToInt(InBox.Max.X) >> HashCellBits;
		int32 MaxY = FMath::FloorToInt(InBox.Max.Y) >> HashCellBits;

		for (int32 y = MinY; y <= MaxY; y++)
		{
			for (int32 x = MinX; x <= MaxX; x++)
			{
				uint64 Key = MakeKey(x, y);
				auto* SetPtr = CellMap.Find(Key);
				if (SetPtr)
				{
					OutInstanceIndices.Append(SetPtr->Array());
				}
			}
		}
	}

	TArray<int32> GetInstancesOverlappingBox(const FBox& InBox) const
	{
		TArray<int32> Result;
		GetInstancesOverlappingBox(InBox, Result);
		return Result;
	}

	void CheckInstanceCount(int32 InCount) const
	{
		int32 HashCount = 0;
		for (const auto& Pair : CellMap)
		{
			HashCount += Pair.Value.Num();
		}

		check(HashCount == InCount);
	}

	void Empty()
	{
		CellMap.Empty();
	}

	friend FArchive& operator<<(FArchive& Ar, FFoliageInstanceHash& Hash)
	{
		Ar << Hash.CellMap;
		return Ar;
	}
};
#endif

/** This is kind of a hack, but is needed right now for backwards compat of code. We use it to describe the placement mode (procedural vs manual)*/
namespace EFoliagePlacementMode
{
	enum Type
	{
		Manual = 0,
		Procedural = 1,
	};

}

/** Used to define a vector along which we'd like to spawn an instance. */
struct FDesiredFoliageInstance
{
	FDesiredFoliageInstance()
		: FoliageType(nullptr)
		, StartTrace(ForceInit)
		, EndTrace(ForceInit)
		, Rotation(ForceInit)
		, TraceRadius(0.f)
		, Age(0.f)
		, PlacementMode(EFoliagePlacementMode::Manual)
	{

	}

	FDesiredFoliageInstance(const FVector& InStartTrace, const FVector& InEndTrace, const float InTraceRadius = 0.f)
		: FoliageType(nullptr)
		, StartTrace(InStartTrace)
		, EndTrace(InEndTrace)
		, Rotation(ForceInit)
		, TraceRadius(InTraceRadius)
		, Age(0.f)
		, PlacementMode(EFoliagePlacementMode::Manual)
	{
	}

	const UFoliageType* FoliageType;
	FGuid ProceduralGuid;
	FVector StartTrace;
	FVector EndTrace;
	FQuat Rotation;
	float TraceRadius;
	float Age;
	const struct FBodyInstance* ProceduralVolumeBodyInstance;
	EFoliagePlacementMode::Type PlacementMode;
};

#if WITH_EDITOR
// Struct to hold potential instances we've sampled
struct FOLIAGE_API FPotentialInstance
{
	FVector HitLocation;
	FVector HitNormal;
	UPrimitiveComponent* HitComponent;
	float HitWeight;
	FDesiredFoliageInstance DesiredInstance;

	FPotentialInstance(FVector InHitLocation, FVector InHitNormal, UPrimitiveComponent* InHitComponent, float InHitWeight, const FDesiredFoliageInstance& InDesiredInstance = FDesiredFoliageInstance());
	bool PlaceInstance(const UWorld* InWorld, const UFoliageType* Settings, FFoliageInstance& Inst, bool bSkipCollision = false);
};
#endif
