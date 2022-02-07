// Copyright Epic Games, Inc. All Rights Reserved.

#include "LWComponentTestFarmPlot.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"

//@TODO: Can add a ReadyToHarvest tag Fragment on when things are ready to harvest, to stop them ticking and signal that we need to create an icon


//////////////////////////////////////////////////////////////////////////
// UFragmentUpdateSystem

void UFragmentUpdateSystem::PostInitProperties()
{
	Super::PostInitProperties();
	ConfigureQueries();
}

//////////////////////////////////////////////////////////////////////////
// UFarmHarvestTimerSetIcon

void UFarmHarvestTimerSetIcon::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(SET_ICON_SET_ICON_SET_ICON);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context) {

		const int32 NumEntities = Context.GetNumEntities();
		TConstArrayView<FFarmGridCellData> GridCoordList = Context.GetFragmentView<FFarmGridCellData>();
		TArrayView<FFarmVisualFragment> VisualList = Context.GetMutableFragmentView<FFarmVisualFragment>();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			const FFarmGridCellData& GridCells = GridCoordList[i];

			const FVector IconPosition(GridCells.CellX*GridCellWidth, GridCells.CellY*GridCellHeight, HarvestIconHeight);
			const FTransform IconTransform(FQuat::Identity, IconPosition, FVector(HarvestIconScale, HarvestIconScale, HarvestIconScale));

			VisualList[i].HarvestIconIndex = HarvestIconISMC->AddInstance(IconTransform);

			FMassEntityHandle ThisEntity = Context.GetEntity(i);
			Context.Defer().RemoveTag<FFarmJustBecameReadyToHarvestTag>(ThisEntity);
			Context.Defer().AddTag<FFarmReadyToHarvestTag>(ThisEntity);
		}
	});
}

//////////////////////////////////////////////////////////////////////////
// ALWFragmentTestFarmPlot

ALWFragmentTestFarmPlot::ALWFragmentTestFarmPlot()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;


	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComp"));

	RootComponent = SceneComponent;

	HarvestIconISMC = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HarvestIconISMC"));
	HarvestIconISMC->SetupAttachment(SceneComponent);
	HarvestIconISMC->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}

void ALWFragmentTestFarmPlot::AddItemToGrid(UMassEntitySubsystem* EntitySystem, uint16 X, uint16 Y, FMassArchetypeHandle Archetype, uint16 VisualIndex)
{
	FMassEntityHandle NewItem = EntitySystem->CreateEntity(Archetype);
	PlantedSquares[X + Y * GridWidth] = NewItem;

	EntitySystem->GetFragmentDataChecked<FFarmWaterFragment>(NewItem).DeltaWaterPerSecond = FMath::FRandRange(-0.01f, -0.001f);
	EntitySystem->GetFragmentDataChecked<FHarvestTimerFragment>(NewItem).NumSecondsLeft = 5 + (FMath::Rand() % 100);

	FFarmGridCellData GridCoords;
	GridCoords.CellX = X;
	GridCoords.CellY = Y;
	EntitySystem->GetFragmentDataChecked<FFarmGridCellData>(NewItem) = GridCoords;

	const FVector MeshPosition(X*GridCellWidth, Y*GridCellHeight, 0.0f);
	const FRotator MeshRotation(0.0f, FMath::FRand()*360.0f, 0.0f);
	const FVector MeshScale(1.0f, 1.0f, 1.0f); //@TODO: plumb in scale param?
	const FTransform MeshTransform(MeshRotation, MeshPosition, MeshScale);

	FFarmVisualFragment& VisualComp = EntitySystem->GetFragmentDataChecked<FFarmVisualFragment>(NewItem);
	VisualComp.VisualType = VisualIndex;
	VisualComp.InstanceIndex = VisualDataISMCs[VisualComp.VisualType]->AddInstance(MeshTransform);
}

void ALWFragmentTestFarmPlot::BeginPlay()
{
	Super::BeginPlay();

	UMassEntitySubsystem* EntitySystem = UWorld::GetSubsystem<UMassEntitySubsystem>(GetWorld());

	FMassArchetypeHandle CropArchetype = EntitySystem->CreateArchetype(TArray<const UScriptStruct*>{ FFarmWaterFragment::StaticStruct(), FFarmCropFragment::StaticStruct(), FHarvestTimerFragment::StaticStruct(), FFarmVisualFragment::StaticStruct(), FFarmGridCellData::StaticStruct() });
	FMassArchetypeHandle FlowerArchetype = EntitySystem->CreateArchetype(TArray<const UScriptStruct*>{ FFarmWaterFragment::StaticStruct(), FFarmFlowerFragment::StaticStruct(), FHarvestTimerFragment::StaticStruct(), FFarmVisualFragment::StaticStruct(), FFarmGridCellData::StaticStruct() });

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

	for (uint16 Y = 0; Y < GridHeight; ++Y)
	{
		for (uint16 X = 0; X < GridWidth; ++X)
		{
			const bool bIsOdd = ((X + Y) & 1) != 0;
			const uint16 VisualIndex = bIsOdd ? TestDataCropIndicies[FMath::RandRange(0, TestDataCropIndicies.Num() - 1)] : TestDataFlowerIndicies[FMath::RandRange(0, TestDataFlowerIndicies.Num() - 1)];

			AddItemToGrid(EntitySystem, X, Y, bIsOdd ? CropArchetype : FlowerArchetype, VisualIndex);
		}
	}
}

void ALWFragmentTestFarmPlot::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);

	QUICK_SCOPE_CYCLE_COUNTER(HeyaTick);

	UMassEntitySubsystem* EntitySystem = UWorld::GetSubsystem<UMassEntitySubsystem>(GetWorld());

	// Run every frame systems
	{
		FMassExecutionContext ExecContext(DeltaTime);
		TSharedPtr<FMassCommandBuffer> DeferredCommandBuffer = MakeShareable(new FMassCommandBuffer());
		ExecContext.SetDeferredCommandBuffer(DeferredCommandBuffer);

		for (UFragmentUpdateSystem* System : PerFrameSystems)
		{
			check(System);
			System->Execute(*EntitySystem, ExecContext);
		}
	}

	// Run per-second systems when it's time
	NextSecondTimer -= DeltaTime;
	while (NextSecondTimer < 0.0f)
	{
		FMassExecutionContext ExecContext(1.f);
		TSharedPtr<FMassCommandBuffer> DeferredCommandBuffer = MakeShareable(new FMassCommandBuffer());
		ExecContext.SetDeferredCommandBuffer(DeferredCommandBuffer);

		NextSecondTimer += 1.0f;
		for (UFragmentUpdateSystem* System : PerSecondSystems)
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
				FMassEntityHandle GridEntity = PlantedSquares[X + Y * GridWidth];
				if (EntitySystem->IsValidEntity(GridEntity))
				{
					FFarmVisualFragment& VisualComp = EntitySystem->GetFragmentDataChecked<FFarmVisualFragment>(GridEntity);
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
	//ForEachFragmentChunk
	// Update visuals

#if 0
	static int32 QQQZZZ = 2;

	const float HarvestIconHeight = 200.0f;

	if (QQQZZZ == 1)
	{
		QUICK_SCOPE_CYCLE_COUNTER(FARM_HARVEST_ICONS);
		EntitySystem->ForEachFragmentChunk<FHarvestTimerFragment>(
			[&](TArrayView<FHarvestTimerFragment> Timers)
		{
			for (FHarvestTimerFragment& Timer : Timers)
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
				FMassEntityHandle GridEntity = PlantedSquares[X + Y * GridWidth];
				if (EntitySystem->IsValidEntity(GridEntity))
				{
					FHarvestTimerFragment& HarvestTimerData = EntitySystem->GetFragmentDataChecked<FHarvestTimerFragment>(GridEntity);
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

