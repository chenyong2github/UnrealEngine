// Copyright Epic Games, Inc. All Rights Reserved.

#include "LWComponentTestFarmPlot.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"

//@TODO: Can add a ReadyToHarvest tag component on when things are ready to harvest, to stop them ticking and signal that we need to create an icon


//////////////////////////////////////////////////////////////////////////
// UComponentUpdateSystem

void UComponentUpdateSystem::PostInitProperties()
{
	Super::PostInitProperties();
	ConfigureQueries();
}

//////////////////////////////////////////////////////////////////////////
// UFarmHarvestTimerSetIcon

void UFarmHarvestTimerSetIcon::Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(SET_ICON_SET_ICON_SET_ICON);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context) {

		const int32 NumEntities = Context.GetEntitiesNum();
		TConstArrayView<FFarmGridCellData> GridCoordList = Context.GetComponentView<FFarmGridCellData>();
		TArrayView<FFarmVisualComponent> VisualList = Context.GetMutableComponentView<FFarmVisualComponent>();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			const FFarmGridCellData& GridCells = GridCoordList[i];

			const FVector IconPosition(GridCells.CellX*GridCellWidth, GridCells.CellY*GridCellHeight, HarvestIconHeight);
			const FTransform IconTransform(FQuat::Identity, IconPosition, FVector(HarvestIconScale, HarvestIconScale, HarvestIconScale));

			VisualList[i].HarvestIconIndex = HarvestIconISMC->AddInstance(IconTransform);

			FLWEntity ThisEntity = Context.GetEntity(i);
			Context.Defer().RemoveTag<FFarmJustBecameReadyToHarvest>(ThisEntity);
			Context.Defer().AddTag<FFarmReadyToHarvest>(ThisEntity);
		}
	});
}

//////////////////////////////////////////////////////////////////////////
// ALWComponentTestFarmPlot

ALWComponentTestFarmPlot::ALWComponentTestFarmPlot()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;


	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComp"));

	RootComponent = SceneComponent;

	HarvestIconISMC = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HarvestIconISMC"));
	HarvestIconISMC->SetupAttachment(SceneComponent);
	HarvestIconISMC->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}

void ALWComponentTestFarmPlot::AddItemToGrid(UMassEntitySubsystem* EntitySystem, int32 X, int32 Y, FArchetypeHandle Archetype, int32 VisualIndex)
{
	FLWEntity NewItem = EntitySystem->CreateEntity(Archetype);
	PlantedSquares[X + Y * GridWidth] = NewItem;

	EntitySystem->GetComponentDataChecked<FFarmWaterComponent>(NewItem).DeltaWaterPerSecond = FMath::FRandRange(-0.01f, -0.001f);
	EntitySystem->GetComponentDataChecked<FHarvestTimerComponent>(NewItem).NumSecondsLeft = 5 + (FMath::Rand() % 100);

	FFarmGridCellData GridCoords;
	GridCoords.CellX = X;
	GridCoords.CellY = Y;
	EntitySystem->GetComponentDataChecked<FFarmGridCellData>(NewItem) = GridCoords;

	const FVector MeshPosition(X*GridCellWidth, Y*GridCellHeight, 0.0f);
	const FRotator MeshRotation(0.0f, FMath::FRand()*360.0f, 0.0f);
	const FVector MeshScale(1.0f, 1.0f, 1.0f); //@TODO: plumb in scale param?
	const FTransform MeshTransform(MeshRotation, MeshPosition, MeshScale);

	FFarmVisualComponent& VisualComp = EntitySystem->GetComponentDataChecked<FFarmVisualComponent>(NewItem);
	VisualComp.VisualType = VisualIndex;
	VisualComp.InstanceIndex = VisualDataISMCs[VisualComp.VisualType]->AddInstance(MeshTransform);
}

void ALWComponentTestFarmPlot::BeginPlay()
{
	Super::BeginPlay();

	UMassEntitySubsystem* EntitySystem = UWorld::GetSubsystem<UMassEntitySubsystem>(GetWorld());

	FArchetypeHandle CropArchetype = EntitySystem->CreateArchetype(TArray<const UScriptStruct*>{ FFarmWaterComponent::StaticStruct(), FFarmCropComponent::StaticStruct(), FHarvestTimerComponent::StaticStruct(), FFarmVisualComponent::StaticStruct(), FFarmGridCellData::StaticStruct() });
	FArchetypeHandle FlowerArchetype = EntitySystem->CreateArchetype(TArray<const UScriptStruct*>{ FFarmWaterComponent::StaticStruct(), FFarmFlowerComponent::StaticStruct(), FHarvestTimerComponent::StaticStruct(), FFarmVisualComponent::StaticStruct(), FFarmGridCellData::StaticStruct() });

	PerFrameSystems.Add(NewObject<UFarmWaterUpdateSystem>(this));

	PerSecondSystems.Add(NewObject<UFarmHarvestTimerSystem_Flowers>(this));
	PerSecondSystems.Add(NewObject<UFarmHarvestTimerSystem_Crops>(this));
	PerSecondSystems.Add(NewObject<UFarmHarvestTimerExpired>(this));

	UFarmHarvestTimerSetIcon* IconSetter = NewObject<UFarmHarvestTimerSetIcon>(this);
	IconSetter->HarvestIconISMC = HarvestIconISMC;
	IconSetter->GridCellWidth = GridCellWidth;
	IconSetter->GridCellHeight = GridCellHeight;
	IconSetter->HarvestIconHeight = 200.0f;
	IconSetter->HarvestIconScale = HarvestIconScale;
	PerSecondSystems.Add(IconSetter);

	HarvestIconISMC->SetCullDistances(IconNearCullDistance, IconFarCullDistance);

	// Create ISMCs for each mesh type
	for (const FFarmVisualDataRow& VisualData : VisualDataTable)
	{
		UHierarchicalInstancedStaticMeshComponent* HISMC = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		HISMC->SetStaticMesh(VisualData.Mesh);
		if (VisualData.MaterialOverride != nullptr)
		{
			HISMC->SetMaterial(0, VisualData.MaterialOverride);
		}
		HISMC->SetCullDistances(VisualNearCullDistance, VisualFarCullDistance);
		HISMC->SetupAttachment(RootComponent);
		HISMC->RegisterComponent();

		VisualDataISMCs.Add(HISMC);
	}

	// Plant us a grid
	const int32 NumGridCells = GridWidth * GridHeight;
	PlantedSquares.AddDefaulted(NumGridCells);

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const bool bIsOdd = ((X + Y) & 1) != 0;
			const int32 VisualIndex = bIsOdd ? TestDataCropIndicies[FMath::RandRange(0, TestDataCropIndicies.Num() - 1)] : TestDataFlowerIndicies[FMath::RandRange(0, TestDataFlowerIndicies.Num() - 1)];

			AddItemToGrid(EntitySystem, X, Y, bIsOdd ? CropArchetype : FlowerArchetype, VisualIndex);
		}
	}
}

void ALWComponentTestFarmPlot::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);

	QUICK_SCOPE_CYCLE_COUNTER(HeyaTick);

	UMassEntitySubsystem* EntitySystem = UWorld::GetSubsystem<UMassEntitySubsystem>(GetWorld());

	// Run every frame systems
	{
		FLWComponentSystemExecutionContext ExecContext(DeltaTime);
		TSharedPtr<FLWCCommandBuffer> DeferredCommandBuffer = MakeShareable(new FLWCCommandBuffer());
		ExecContext.SetDeferredCommandBuffer(DeferredCommandBuffer);

		for (UComponentUpdateSystem* System : PerFrameSystems)
		{
			check(System);
			System->Execute(*EntitySystem, ExecContext);
		}
	}

	// Run per-second systems when it's time
	NextSecondTimer -= DeltaTime;
	while (NextSecondTimer < 0.0f)
	{
		FLWComponentSystemExecutionContext ExecContext(1.f);
		TSharedPtr<FLWCCommandBuffer> DeferredCommandBuffer = MakeShareable(new FLWCCommandBuffer());
		ExecContext.SetDeferredCommandBuffer(DeferredCommandBuffer);

		NextSecondTimer += 1.0f;
		for (UComponentUpdateSystem* System : PerSecondSystems)
		{
			check(System);
			System->Execute(*EntitySystem, ExecContext);
		}
	}

#if 0
	// Update visuals
	{
		QUICK_SCOPE_CYCLE_COUNTER(FARM_BASE_MESHES);

		for (int32 Y = 0; Y < GridHeight; ++Y)
		{
			for (int32 X = 0; X < GridWidth; ++X)
			{
				FLWEntity GridEntity = PlantedSquares[X + Y * GridWidth];
				if (EntitySystem->IsValidEntity(GridEntity))
				{
					FFarmVisualComponent& VisualComp = EntitySystem->GetComponentDataChecked<FFarmVisualComponent>(GridEntity);
					if (VisualComp.InstanceIndex < 0)
					{
						const FVector MeshPosition(X*GridCellWidth, Y*GridCellHeight, 0.0f);
						const FRotator MeshRotation(0.0f, FMath::FRand()*360.0f, 0.0f);
						const FVector MeshScale(1.0f, 1.0f, 1.0f); //@TODO: plumb in scale param?
						const FTransform MeshTransform(MeshRotation, MeshPosition, MeshScale);

						VisualComp.InstanceIndex = VisualDataISMCs[VisualComp.VisualType]->AddInstance(MeshTransform);
					}
				}
			}
		}
	}
#endif
	//ForEachComponentChunk
	// Update visuals

#if 0
	static int32 QQQZZZ = 2;

	const float HarvestIconHeight = 200.0f;

	if (QQQZZZ == 1)
	{
		QUICK_SCOPE_CYCLE_COUNTER(FARM_HARVEST_ICONS);
		EntitySystem->ForEachComponentChunk<FHarvestTimerComponent>(
			[&](TArrayView<FHarvestTimerComponent> Timers)
		{
			for (FHarvestTimerComponent& Timer : Timers)
			{
				if ((Timer.NumSecondsLeft == 0) && (Timer.HarvestIconIndex < 0))
				{
					const FVector IconPosition(0.0f, 0.0f, HarvestIconHeight);
					const FTransform IconTransform(FQuat::Identity, IconPosition, FVector(HarvestIconScale, HarvestIconScale, HarvestIconScale));

					Timer.HarvestIconIndex = HarvestIconISMC->AddInstance(IconTransform);
				}
			}
		});
	}
	else if (QQQZZZ == 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(FARM_HARVEST_ICONS);

		for (int32 Y = 0; Y < GridHeight; ++Y)
		{
			for (int32 X = 0; X < GridWidth; ++X)
			{
				FLWEntity GridEntity = PlantedSquares[X + Y * GridWidth];
				if (EntitySystem->IsValidEntity(GridEntity))
				{
					FHarvestTimerComponent& HarvestTimerData = EntitySystem->GetComponentDataChecked<FHarvestTimerComponent>(GridEntity);
					if ((HarvestTimerData.NumSecondsLeft == 0) && (HarvestTimerData.HarvestIconIndex < 0))
					{
						const FVector IconPosition(X*GridCellWidth, Y*GridCellHeight, HarvestIconHeight);
						const FTransform IconTransform(FQuat::Identity, IconPosition, FVector(HarvestIconScale, HarvestIconScale, HarvestIconScale));

						HarvestTimerData.HarvestIconIndex = HarvestIconISMC->AddInstance(IconTransform);
					}
				}
			}
		}
	}
#endif
}

//////////////////////////////////////////////////////////////////////////
// UFarmWaterUpdateSystem

