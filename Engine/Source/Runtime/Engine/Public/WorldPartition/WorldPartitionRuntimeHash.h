// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "Engine/World.h"
#include "WorldPartition.h"
#include "WorldPartition/ActorDescList.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionStreamingGeneration.h"
#include "WorldPartition/WorldPartitionStreamingGenerationContext.h"
#if WITH_EDITOR
#include "CookPackageSplitter.h"
#include "Misc/HierarchicalLogArchive.h"
#endif
#include "WorldPartitionRuntimeHash.generated.h"

struct FHierarchicalLogArchive;
class FWorldPartitionDraw2DContext;

UENUM()
enum class EWorldPartitionStreamingPerformance : uint8
{
	Good,
	Slow,
	Critical
};

UCLASS(Abstract)
class ENGINE_API URuntimeHashExternalStreamingObjectBase : public UObject
{
	GENERATED_BODY()

	friend class UWorldPartitionRuntimeHash;

public:
	//~ Begin UObject Interface
	virtual class UWorld* GetWorld() const override final { return GetOwningWorld(); }
	//~ End UObject Interface

	UWorld* GetOwningWorld() const { return OwningWorld.Get(); }
	UWorld* GetOuterWorld() const { return OuterWorld.Get(); }

	void ForEachStreamingCells(TFunctionRef<void(UWorldPartitionRuntimeCell&)> Func);
	
	void OnStreamingObjectLoaded(UWorld* InjectedWorld);

#if WITH_EDITOR
	void PopulateGeneratorPackageForCook();
#endif

	UPROPERTY();
	TMap<FName, FName> SubObjectsToCellRemapping;

protected:
	UPROPERTY();
	TSoftObjectPtr<UWorld> OwningWorld;

	UPROPERTY();
	TSoftObjectPtr<UWorld> OuterWorld;

	UPROPERTY();
	TMap<FName, FName> CellToLevelStreamingPackage;
};

struct FWorldPartitionQueryCache
{
public:
	void AddCellInfo(const UWorldPartitionRuntimeCell* Cell, const FSphericalSector& SourceShape);
	double GetCellMinSquareDist(const UWorldPartitionRuntimeCell* Cell) const;

private:
	TMap<const UWorldPartitionRuntimeCell*, double> CellToSourceMinSqrDistances;
};

UCLASS(Abstract, Config=Engine, AutoExpandCategories=(WorldPartition), Within=WorldPartition)
class ENGINE_API UWorldPartitionRuntimeHash : public UObject
{
	GENERATED_UCLASS_BODY()

	friend class URuntimePartition;

#if WITH_EDITOR
	virtual void SetDefaultValues() {}
	virtual bool SupportsHLODs() const { return false; }
	virtual TArray<UWorldPartitionRuntimeCell*> GetAlwaysLoadedCells() const;
	virtual bool GenerateStreaming(class UWorldPartitionStreamingPolicy* StreamingPolicy, const IStreamingGenerationContext* StreamingGenerationContext, TArray<FString>* OutPackagesToGenerate);
	virtual void FlushStreaming();
	virtual bool GenerateHLOD(ISourceControlHelper* SourceControlHelper, const IStreamingGenerationContext* StreamingGenerationContext, bool bCreateActorsOnly) const { return false; }
	virtual bool IsValidGrid(FName GridName) const { return false; }
	virtual void DrawPreview() const {}

	virtual URuntimeHashExternalStreamingObjectBase* StoreToExternalStreamingObject(UObject* StreamingObjectOuter, FName StreamingObjectName) { return nullptr; }

	virtual void DumpStateLog(FHierarchicalLogArchive& Ar) const;

	// non-virtual
	bool PrepareGeneratorPackageForCook(TArray<UPackage*>& OutModifiedPackages);
	bool PopulateGeneratorPackageForCook(const TArray<FWorldPartitionCookPackage*>& PackagesToCook, TArray<UPackage*>& OutModifiedPackages);
	bool PopulateGeneratedPackageForCook(const FWorldPartitionCookPackage& PackagesToCook, TArray<UPackage*>& OutModifiedPackages);
	UWorldPartitionRuntimeCell* GetCellForPackage(const FWorldPartitionCookPackage& PackageToCook) const;

	// PIE/Game methods
	void OnBeginPlay();
	void OnEndPlay();

protected:
	bool ConditionalRegisterAlwaysLoadedActorsForPIE(const IStreamingGenerationContext::FActorSetInstance* ActorSetInstance, bool bIsMainWorldPartition, bool bIsMainContainer, bool bIsCellAlwaysLoaded);
	bool PopulateCellActorInstances(const TArray<const IStreamingGenerationContext::FActorSetInstance*>& ActorSetInstances, bool bIsMainWorldPartition, bool bIsCellAlwaysLoaded, TArray<IStreamingGenerationContext::FActorInstance>& OutCellActorInstances);
	void PopulateRuntimeCell(UWorldPartitionRuntimeCell* RuntimeCell, const TArray<IStreamingGenerationContext::FActorInstance>& ActorInstances, TArray<FString>* OutPackagesToGenerate);
#endif

public:
	class FStreamingSourceCells
	{
	public:
		void AddCell(const UWorldPartitionRuntimeCell* Cell, const FWorldPartitionStreamingSource& Source, const FSphericalSector& SourceShape);
		void Reset() { Cells.Reset(); }
		int32 Num() const { return Cells.Num(); }
		TSet<const UWorldPartitionRuntimeCell*>& GetCells() { return Cells; }

	private:
		TSet<const UWorldPartitionRuntimeCell*> Cells;
	};

	// Deprecated streaming interface
	UE_DEPRECATED(5.1, "GetAllStreamingCells is deprecated, use ForEachStreamingCells instead.")
	int32 GetAllStreamingCells(TSet<const UWorldPartitionRuntimeCell*>& Cells, bool bAllDataLayers = false, bool bDataLayersOnly = false, const TSet<FName>& InDataLayers = TSet<FName>()) const;

	UE_DEPRECATED(5.1, "GetStreamingCells is deprecated, use ForEachStreamingCells instead.")
	bool GetStreamingCells(const FWorldPartitionStreamingQuerySource& QuerySource, TSet<const UWorldPartitionRuntimeCell*>& OutCells) const;

	UE_DEPRECATED(5.1, "GetStreamingCells is deprecated, use ForEachStreamingCells instead.")
	bool GetStreamingCells(const TArray<FWorldPartitionStreamingSource>& Sources, UWorldPartitionRuntimeHash::FStreamingSourceCells& OutActivateCells, UWorldPartitionRuntimeHash::FStreamingSourceCells& OutLoadCells) const;

	// Streaming interface
	virtual void ForEachStreamingCells(TFunctionRef<bool(const UWorldPartitionRuntimeCell*)> Func) const {}
	virtual void ForEachStreamingCellsQuery(const FWorldPartitionStreamingQuerySource& QuerySource, TFunctionRef<bool(const UWorldPartitionRuntimeCell*)> Func, FWorldPartitionQueryCache* QueryCache = nullptr) const {}
	virtual void ForEachStreamingCellsSources(const TArray<FWorldPartitionStreamingSource>& Sources, TFunctionRef<bool(const UWorldPartitionRuntimeCell*, EStreamingSourceTargetState)> Func) const {}
	// Computes a hash value of all runtime hash specific dependencies that affects the update of the streaming
	virtual uint32 ComputeUpdateStreamingHash() const { return 0; }

	bool IsCellRelevantFor(bool bClientOnlyVisible) const;
	EWorldPartitionStreamingPerformance GetStreamingPerformance(const TSet<const UWorldPartitionRuntimeCell*>& CellToActivate) const;

	virtual bool InjectExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* ExternalStreamingObject);
	virtual bool RemoveExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* ExternalStreamingObject);

	virtual bool Draw2D(FWorldPartitionDraw2DContext& DrawContext) const { return false; }
	virtual void Draw3D(const TArray<FWorldPartitionStreamingSource>& Sources) const {}
	virtual bool ContainsRuntimeHash(const FString& Name) const { return false; }
	virtual bool IsStreaming3D() const { return true; }

protected:
	static URuntimeHashExternalStreamingObjectBase* CreateExternalStreamingObject(TSubclassOf<URuntimeHashExternalStreamingObjectBase> InClass, UObject* InOuter, FName InName, UWorld* InOwningWorld, UWorld* InOuterWorld);
	UWorldPartitionRuntimeCell* CreateRuntimeCell(UClass* CellClass, UClass* CellDataClass, const FString& CellName, const FString& CellInstanceSuffix, UObject* InOuter = nullptr);
	virtual EWorldPartitionStreamingPerformance GetStreamingPerformanceForCell(const UWorldPartitionRuntimeCell* Cell) const { return EWorldPartitionStreamingPerformance::Good; }

#if WITH_EDITOR
	template <class T>
	T* CreateExternalStreamingObject(UObject* InOuter, FName InName)
	{
		return static_cast<T*>(CreateExternalStreamingObject(T::StaticClass(), InOuter, InName, GetWorld(), GetTypedOuter<UWorld>()));
	}
#endif

#if WITH_EDITORONLY_DATA
	struct FAlwaysLoadedActorForPIE
	{
		FAlwaysLoadedActorForPIE(const FWorldPartitionReference& InReference, AActor* InActor)
			: Reference(InReference), Actor(InActor)
		{}

		FWorldPartitionReference Reference;
		TWeakObjectPtr<AActor> Actor;
	};

	TArray<FAlwaysLoadedActorForPIE> AlwaysLoadedActorsForPIE;

	TMap<FString, UWorldPartitionRuntimeCell*> PackagesToGenerateForCook;

public:
	mutable FActorDescList ModifiedActorDescListForPIE;
#endif

private:
#if WITH_EDITOR
	void ForceExternalActorLevelReference(bool bForceExternalActorLevelReferenceForPIE);
#endif

	TSet<TWeakObjectPtr<URuntimeHashExternalStreamingObjectBase>> InjectedExternalStreamingObjects;
};