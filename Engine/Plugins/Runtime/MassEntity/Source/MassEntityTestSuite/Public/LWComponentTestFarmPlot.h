// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"

#include "LWComponentTypes.h"
#include "EntitySubsystem.h"
#include "LWCCommandBuffer.h"

#include "LWComponentTestFarmPlot.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UHierarchicalInstancedStaticMeshComponent;

USTRUCT()
struct FFarmVisualDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category=Farm)
	UStaticMesh* Mesh = nullptr;

	UPROPERTY(EditAnywhere, Category=Farm)
	UMaterialInterface* MaterialOverride = nullptr;
};

USTRUCT()
struct FFarmJustBecameReadyToHarvest : public FComponentTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FFarmReadyToHarvest : public FComponentTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FFarmGridCellData : public FLWComponentData
{
	GENERATED_BODY()

	uint16 CellX = 0;
	uint16 CellY = 0;
};

USTRUCT()
struct FFarmWaterComponent : public FLWComponentData
{
	GENERATED_BODY()

	float CurrentWater = 1.0f;
	float DeltaWaterPerSecond = -0.01f;
};

USTRUCT()
struct FFarmFlowerComponent : public FLWComponentData
{
	GENERATED_BODY()
		
	uint32 NumBonusTicks = 0;
	uint16 FlowerType = 0;
};

USTRUCT()
struct FFarmCropComponent : public FLWComponentData
{
	GENERATED_BODY()

	uint16 CropType = 0;
};


USTRUCT()
struct FFarmVisualComponent : public FLWComponentData
{
	GENERATED_BODY()

	int32 InstanceIndex = -1;
	int32 HarvestIconIndex = -1;
	uint16 VisualType = 0;
};

USTRUCT()
struct FHarvestTimerComponent : public FLWComponentData
{
	GENERATED_BODY()

	uint32 NumSecondsLeft = 15;
};


UCLASS()
class UComponentUpdateSystem : public UObject
{
	GENERATED_BODY()
public:
	virtual void PostInitProperties() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) {}
protected:
	virtual void ConfigureQueries() {}
protected:
	FLWComponentQuery EntityQuery;
};


UCLASS()
class UFarmWaterUpdateSystem : public UComponentUpdateSystem
{
	GENERATED_BODY()

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FFarmWaterComponent>(ELWComponentAccess::ReadWrite);
	}

	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(UFarmWaterUpdateSystem_Run);
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context) {

			TArrayView<FFarmWaterComponent> WaterList = Context.GetMutableComponentView<FFarmWaterComponent>();

			for (FFarmWaterComponent& WaterComponent : WaterList)
			{
				WaterComponent.CurrentWater = FMath::Clamp(WaterComponent.CurrentWater + WaterComponent.DeltaWaterPerSecond * 0.01f, 0.0f, 1.0f);
			}
		});
	}
};


UCLASS()
class UFarmHarvestTimerSystem_Flowers : public UComponentUpdateSystem
{
	GENERATED_BODY()

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FHarvestTimerComponent>(ELWComponentAccess::ReadWrite);
		EntityQuery.AddRequirement<FFarmWaterComponent>(ELWComponentAccess::ReadOnly);
		EntityQuery.AddRequirement<FFarmFlowerComponent>(ELWComponentAccess::ReadWrite);
	}

	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(UFarmHarvestTimerSystem_Flowers_Run);
		
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context) {
			const int32 NumEntities = Context.GetEntitiesNum();
			const float WellWateredThreshold = 0.25f;
			TArrayView<FHarvestTimerComponent> TimerList = Context.GetMutableComponentView<FHarvestTimerComponent>();
			TConstArrayView<FFarmWaterComponent> WaterList = Context.GetComponentView<FFarmWaterComponent>();
			TArrayView<FFarmFlowerComponent> FlowerList = Context.GetMutableComponentView<FFarmFlowerComponent>();
			
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
class UFarmHarvestTimerSystem_Crops : public UComponentUpdateSystem
{
	GENERATED_BODY()

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FHarvestTimerComponent>(ELWComponentAccess::ReadWrite);
		EntityQuery.AddRequirement<FFarmWaterComponent>(ELWComponentAccess::ReadOnly);
		EntityQuery.AddRequirement<FFarmCropComponent>(ELWComponentAccess::ReadOnly);
	}

	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(UFarmHarvestTimerSystem_Crops_Run);
		
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context) {
			
			const int32 NumEntities = Context.GetEntitiesNum();
			const float WellWateredThreshold = 0.25f;
			TArrayView<FHarvestTimerComponent> TimerList = Context.GetMutableComponentView<FHarvestTimerComponent>();
			TConstArrayView<FFarmWaterComponent> WaterList = Context.GetMutableComponentView<FFarmWaterComponent>();
				
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
class UFarmHarvestTimerExpired : public UComponentUpdateSystem
{
	GENERATED_BODY()

public:
	virtual void ConfigureQueries() override
	{
		EntityQuery.AddRequirement<FHarvestTimerComponent>(ELWComponentAccess::ReadOnly);
		EntityQuery.AddTagRequirement<FFarmJustBecameReadyToHarvest>(ELWComponentPresence::None);
		EntityQuery.AddTagRequirement<FFarmReadyToHarvest>(ELWComponentPresence::None);
	}

	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(UFarmHarvestTimerExpired_Run);
		
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context) {
			const int32 NumEntities = Context.GetEntitiesNum();
			TConstArrayView<FHarvestTimerComponent> TimerList = Context.GetComponentView<FHarvestTimerComponent>();

			for (int32 i = 0; i < NumEntities; ++i)
			{
				if (TimerList[i].NumSecondsLeft == 0)
				{
					Context.Defer().AddTag<FFarmJustBecameReadyToHarvest>(Context.GetEntity(i));
				}
			}
		});
	}
};


//////////////////////////////////////////////////////////////////////

UCLASS()
class UFarmHarvestTimerSetIcon : public UComponentUpdateSystem
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
		EntityQuery.AddRequirement<FFarmGridCellData>(ELWComponentAccess::ReadOnly);
		EntityQuery.AddRequirement<FFarmVisualComponent>(ELWComponentAccess::ReadWrite);
		EntityQuery.AddTagRequirement<FFarmJustBecameReadyToHarvest>(ELWComponentPresence::All);
	}

	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;
};

//////////////////////////////////////////////////////////////////////


UCLASS(config=Game)
class ALWComponentTestFarmPlot : public AActor
{
	GENERATED_BODY()

public:
	int32 GridWidth = 40*7;
	int32 GridHeight = 20*7;

	UPROPERTY(EditAnywhere, Category=Farm)
	float GridCellWidth = 150.0f;
	
	UPROPERTY(EditAnywhere, Category=Farm)
	float GridCellHeight = 150.0f;
	
	UPROPERTY(EditAnywhere, Category=Farm)
	float HarvestIconScale = 0.3f;
	
	TArray<FLWEntity> PlantedSquares;

	
	UPROPERTY(EditDefaultsOnly, Category=Farm)
	TArray<FFarmVisualDataRow> VisualDataTable;

	UPROPERTY(Transient)
	TArray<UHierarchicalInstancedStaticMeshComponent*> VisualDataISMCs;

	float NextSecondTimer = 0.0f;

	UPROPERTY(Transient)
	TArray<UComponentUpdateSystem*> PerFrameSystems;

	UPROPERTY(Transient)
	TArray<UComponentUpdateSystem*> PerSecondSystems;

	// Indicies into VisualDataTable for flowers
	UPROPERTY(EditAnywhere, Category=Farm)
	TArray<int32> TestDataFlowerIndicies;

	// Indicies into VisualDataTable for crops
	UPROPERTY(EditAnywhere, Category=Farm)
	TArray<int32> TestDataCropIndicies;

	UPROPERTY(EditAnywhere, Category = Farm)
	float VisualNearCullDistance = 1000.0f;

	UPROPERTY(EditAnywhere, Category = Farm)
	float VisualFarCullDistance = 1200.0f;

	UPROPERTY(EditAnywhere, Category = Farm)
	float IconNearCullDistance = 400.0f;

	UPROPERTY(EditAnywhere, Category = Farm)
	float IconFarCullDistance = 800.0f;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess = "true"), Category=Farm)
	UHierarchicalInstancedStaticMeshComponent* HarvestIconISMC;

	TArray<int32> FreeHarvestIconIndicies;

private:
	void AddItemToGrid(UEntitySubsystem* EntitySystem, int32 X, int32 Y, FArchetypeHandle Archetype, int32 VisualIndex);

public:
	ALWComponentTestFarmPlot();

	virtual void BeginPlay() override;
	virtual void TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;
};
