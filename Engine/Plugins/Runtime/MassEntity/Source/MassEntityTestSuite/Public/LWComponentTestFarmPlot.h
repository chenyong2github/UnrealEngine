// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"

#include "MassEntityTypes.h"
#include "MassEntityManager.h"
#include "MassCommandBuffer.h"

#include "LWComponentTestFarmPlot.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UHierarchicalInstancedStaticMeshComponent;

USTRUCT()
struct FFarmVisualDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category=Farm)
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY(EditAnywhere, Category=Farm)
	TObjectPtr<UMaterialInterface> MaterialOverride = nullptr;
};

USTRUCT()
struct FFarmJustBecameReadyToHarvestTag : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FFarmReadyToHarvestTag : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FFarmGridCellData : public FMassFragment
{
	GENERATED_BODY()

	uint16 CellX = 0;
	uint16 CellY = 0;
};

USTRUCT()
struct FFarmWaterFragment : public FMassFragment
{
	GENERATED_BODY()

	float CurrentWater = 1.0f;
	float DeltaWaterPerSecond = -0.01f;
};

USTRUCT()
struct FFarmFlowerFragment : public FMassFragment
{
	GENERATED_BODY()
		
	uint32 NumBonusTicks = 0;
	uint16 FlowerType = 0;
};

USTRUCT()
struct FFarmCropFragment : public FMassFragment
{
	GENERATED_BODY()

	uint16 CropType = 0;
};


USTRUCT()
struct FFarmVisualFragment : public FMassFragment
{
	GENERATED_BODY()

	int32 InstanceIndex = -1;
	int32 HarvestIconIndex = -1;
	uint16 VisualType = 0;
};

USTRUCT()
struct FHarvestTimerFragment : public FMassFragment
{
	GENERATED_BODY()

	uint32 NumSecondsLeft = 15;
};


UCLASS()
class UFragmentUpdateSystem : public UObject
{
	GENERATED_BODY()
public:
	virtual void PostInitProperties() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) {}
protected:
	virtual void ConfigureQueries() {}
protected:
	FMassEntityQuery EntityQuery;
};


UCLASS()
class UFarmWaterUpdateSystem : public UFragmentUpdateSystem
{
	GENERATED_BODY()

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FFarmWaterFragment>(EMassFragmentAccess::ReadWrite);
	}

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(UFarmWaterUpdateSystem_Run);
		EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context) {

			TArrayView<FFarmWaterFragment> WaterList = Context.GetMutableFragmentView<FFarmWaterFragment>();

			for (FFarmWaterFragment& WaterFragment : WaterList)
			{
				WaterFragment.CurrentWater = FMath::Clamp(WaterFragment.CurrentWater + WaterFragment.DeltaWaterPerSecond * 0.01f, 0.0f, 1.0f);
			}
		});
	}
};


UCLASS()
class UFarmHarvestTimerSystem_Flowers : public UFragmentUpdateSystem
{
	GENERATED_BODY()

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FHarvestTimerFragment>(EMassFragmentAccess::ReadWrite);
		EntityQuery.AddRequirement<FFarmWaterFragment>(EMassFragmentAccess::ReadOnly);
		EntityQuery.AddRequirement<FFarmFlowerFragment>(EMassFragmentAccess::ReadWrite);
	}

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(UFarmHarvestTimerSystem_Flowers_Run);
		
		EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context) {
			const int32 NumEntities = Context.GetNumEntities();
			const float WellWateredThreshold = 0.25f;
			TArrayView<FHarvestTimerFragment> TimerList = Context.GetMutableFragmentView<FHarvestTimerFragment>();
			TConstArrayView<FFarmWaterFragment> WaterList = Context.GetFragmentView<FFarmWaterFragment>();
			TArrayView<FFarmFlowerFragment> FlowerList = Context.GetMutableFragmentView<FFarmFlowerFragment>();
			
			for (int32 i = 0; i < NumEntities; ++i)
			{
				if (TimerList[i].NumSecondsLeft > 0)
				{
					--TimerList[i].NumSecondsLeft;

					if (WaterList[i].CurrentWater > WellWateredThreshold)
					{
						++FlowerList[i].NumBonusTicks;
					}
				}
			}
		});
	}
};




UCLASS()
class UFarmHarvestTimerSystem_Crops : public UFragmentUpdateSystem
{
	GENERATED_BODY()

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FHarvestTimerFragment>(EMassFragmentAccess::ReadWrite);
		EntityQuery.AddRequirement<FFarmWaterFragment>(EMassFragmentAccess::ReadOnly);
		EntityQuery.AddRequirement<FFarmCropFragment>(EMassFragmentAccess::ReadOnly);
	}

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(UFarmHarvestTimerSystem_Crops_Run);
		
		EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context) {
			
			const int32 NumEntities = Context.GetNumEntities();
			const float WellWateredThreshold = 0.25f;
			TArrayView<FHarvestTimerFragment> TimerList = Context.GetMutableFragmentView<FHarvestTimerFragment>();
			TConstArrayView<FFarmWaterFragment> WaterList = Context.GetMutableFragmentView<FFarmWaterFragment>();
				
			for (int32 i = 0; i < NumEntities; ++i)
			{
				const uint32 TimeToSubtract = (WaterList[i].CurrentWater > WellWateredThreshold) ? 2 : 1;
				TimerList[i].NumSecondsLeft = (TimerList[i].NumSecondsLeft >= TimeToSubtract) ? (TimerList[i].NumSecondsLeft - TimeToSubtract) : 0;
			}
		});
	}
};

//////////////////////////////////////////////////////////////////////

UCLASS()
class UFarmHarvestTimerExpired : public UFragmentUpdateSystem
{
	GENERATED_BODY()

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FHarvestTimerFragment>(EMassFragmentAccess::ReadOnly);
		EntityQuery.AddTagRequirement<FFarmJustBecameReadyToHarvestTag>(EMassFragmentPresence::None);
		EntityQuery.AddTagRequirement<FFarmReadyToHarvestTag>(EMassFragmentPresence::None);
	}

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(UFarmHarvestTimerExpired_Run);
		
		EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context) {
			const int32 NumEntities = Context.GetNumEntities();
			TConstArrayView<FHarvestTimerFragment> TimerList = Context.GetFragmentView<FHarvestTimerFragment>();

			for (int32 i = 0; i < NumEntities; ++i)
			{
				if (TimerList[i].NumSecondsLeft == 0)
				{
					Context.Defer().AddTag<FFarmJustBecameReadyToHarvestTag>(Context.GetEntity(i));
				}
			}
		});
	}
};


//////////////////////////////////////////////////////////////////////

UCLASS()
class UFarmHarvestTimerSetIcon : public UFragmentUpdateSystem
{
	GENERATED_BODY()

public:
	UHierarchicalInstancedStaticMeshComponent* HarvestIconISMC;
	float GridCellWidth;
	float GridCellHeight;
	float HarvestIconHeight;
	float HarvestIconScale;

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FFarmGridCellData>(EMassFragmentAccess::ReadOnly);
		EntityQuery.AddRequirement<FFarmVisualFragment>(EMassFragmentAccess::ReadWrite);
		EntityQuery.AddTagRequirement<FFarmJustBecameReadyToHarvestTag>(EMassFragmentPresence::All);
	}

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

//////////////////////////////////////////////////////////////////////


UCLASS(config=Game)
class ALWFragmentTestFarmPlot : public AActor
{
	GENERATED_BODY()

public:
	uint16 GridWidth = 40*7;
	uint16 GridHeight = 20*7;

	UPROPERTY(EditAnywhere, Category=Farm)
	float GridCellWidth = 150.0f;
	
	UPROPERTY(EditAnywhere, Category=Farm)
	float GridCellHeight = 150.0f;
	
	UPROPERTY(EditAnywhere, Category=Farm)
	float HarvestIconScale = 0.3f;
	
	TArray<FMassEntityHandle> PlantedSquares;

	
	UPROPERTY(EditDefaultsOnly, Category=Farm)
	TArray<FFarmVisualDataRow> VisualDataTable;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> VisualDataISMCs;

	float NextSecondTimer = 0.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFragmentUpdateSystem>> PerFrameSystems;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFragmentUpdateSystem>> PerSecondSystems;

	// Indicies into VisualDataTable for flowers
	UPROPERTY(EditAnywhere, Category=Farm)
	TArray<uint16> TestDataFlowerIndicies;

	// Indicies into VisualDataTable for crops
	UPROPERTY(EditAnywhere, Category=Farm)
	TArray<uint16> TestDataCropIndicies;

	UPROPERTY(EditAnywhere, Category = Farm)
	uint32 VisualNearCullDistance = 1000;

	UPROPERTY(EditAnywhere, Category = Farm)
	uint32 VisualFarCullDistance = 1200;

	UPROPERTY(EditAnywhere, Category = Farm)
	uint32 IconNearCullDistance = 400;

	UPROPERTY(EditAnywhere, Category = Farm)
	uint32 IconFarCullDistance = 800;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess = "true"), Category=Farm)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HarvestIconISMC;

	TArray<int32> FreeHarvestIconIndicies;

private:
	void AddItemToGrid(FMassEntityManager& EntityManager, uint16 X, uint16 Y, FMassArchetypeHandle Archetype, uint16 VisualIndex);

public:
	ALWFragmentTestFarmPlot();

	virtual void BeginPlay() override;
	virtual void TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;
};
