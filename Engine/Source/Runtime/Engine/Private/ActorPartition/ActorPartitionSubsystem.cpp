// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorPartition/ActorPartitionSubsystem.h"
#include "ActorPartition/PartitionActor.h"
#include "WorldPartition/ActorPartition/PartitionActorDesc.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogActorPartitionSubsystem, All, All);

#if WITH_EDITOR

FActorPartitionGetParams::FActorPartitionGetParams(const TSubclassOf<APartitionActor>& InActorClass, bool bInCreate, ULevel* InLevelHint, const FVector& InLocationHint)
	: ActorClass(InActorClass)
	, bCreate(bInCreate)
	, LocationHint(InLocationHint)
	, LevelHint(InLevelHint)
{
}

void FActorPartitionGridHelper::ForEachIntersectingCell(const TSubclassOf<APartitionActor>& InActorClass, const FBox& InBounds, ULevel* InLevel, TFunctionRef<bool(const UActorPartitionSubsystem::FCellCoord&, const FBox&)> InOperation)
{
	uint32 GridSize = InActorClass->GetDefaultObject<APartitionActor>()->GetDefaultGridSize(InLevel->GetWorld());
	UActorPartitionSubsystem::FCellCoord MinCellCoords = UActorPartitionSubsystem::FCellCoord::GetCellCoord(InBounds.Min, InLevel, GridSize);
	UActorPartitionSubsystem::FCellCoord MaxCellCoords = UActorPartitionSubsystem::FCellCoord::GetCellCoord(InBounds.Max, InLevel, GridSize);

	for (int32 z = MinCellCoords.Z; z <= MaxCellCoords.Z; z++)
	{
		for (int32 y = MinCellCoords.Y; y <= MaxCellCoords.Y; y++)
		{
			for (int32 x = MinCellCoords.X; x <= MaxCellCoords.X; x++)
			{
				UActorPartitionSubsystem::FCellCoord CellCoords(x, y, z, InLevel);
				const FVector Min = FVector(
					CellCoords.X * GridSize,
					CellCoords.Y * GridSize,
					CellCoords.Z * GridSize
				);
				const FVector Max = Min + FVector(GridSize);
				FBox CellBounds(Min, Max);

				if (!InOperation(MoveTemp(CellCoords), MoveTemp(CellBounds)))
				{
					return;
				}
			}
		}
	}
}

/**
 * FActorPartitionLevel
 */
class FActorPartitionLevel : public FBaseActorPartition
{
public:
	FActorPartitionLevel(UWorld* InWorld)
		: FBaseActorPartition(InWorld) 
	{
		 LevelRemovedFromWorldHandle = FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FActorPartitionLevel::OnLevelRemovedFromWorld);
	}

	~FActorPartitionLevel()
	{
		FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedFromWorldHandle);
	}

	UActorPartitionSubsystem::FCellCoord GetActorPartitionHash(const FActorPartitionGetParams& GetParams) const override 
	{ 
		ULevel* SpawnLevel = GetSpawnLevel(GetParams.LevelHint, GetParams.LocationHint);
		return UActorPartitionSubsystem::FCellCoord(0, 0, 0, SpawnLevel);
	}
	
	APartitionActor* GetActor(const TSubclassOf<APartitionActor>& InActorClass, bool bInCreate, const UActorPartitionSubsystem::FCellCoord& InCellCoord) override
	{
		check(InCellCoord.Level);
		
		APartitionActor* FoundActor = nullptr;
		for (AActor* Actor : InCellCoord.Level->Actors)
		{
			if (APartitionActor* PartitionActor = Cast<APartitionActor>(Actor))
			{
				FoundActor = PartitionActor;
				break;
			}
		}
		
		if (!FoundActor && bInCreate)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = InCellCoord.Level;
			FoundActor = CastChecked<APartitionActor>(World->SpawnActor(InActorClass, nullptr, nullptr, SpawnParams));
		}

		check(FoundActor || !bInCreate);
		return FoundActor;
	}

private:
	void OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
	{
		if (InWorld == World)
		{
			GetOnActorPartitionHashInvalidated().Broadcast(UActorPartitionSubsystem::FCellCoord(0, 0, 0, InLevel));
		}
	}

	ULevel* GetSpawnLevel(ULevel* InLevelHint, const FVector& InLocationHint) const
	{
		check(InLevelHint);
		ULevel* SpawnLevel = InLevelHint;
		if (ILevelPartitionInterface* LevelPartition = InLevelHint->GetLevelPartition())
		{
			if (ULevel* SubLevel = LevelPartition->GetSubLevel(InLocationHint))
			{
				SpawnLevel = SubLevel;
			}
		}
		return SpawnLevel;
	}

	FDelegateHandle LevelRemovedFromWorldHandle;
};

/**
 * FActorPartitionWorldPartition
 */
class FActorPartitionWorldPartition : public FBaseActorPartition
{
public:
	FActorPartitionWorldPartition(UWorld* InWorld)
		: FBaseActorPartition(InWorld)
	{
		WorldPartition = InWorld->GetSubsystem<UWorldPartitionSubsystem>();
		check(WorldPartition || IsRunningCommandlet());
	}

	UActorPartitionSubsystem::FCellCoord GetActorPartitionHash(const FActorPartitionGetParams& GetParams) const override 
	{
		uint32 GridSize = GetParams.ActorClass->GetDefaultObject<APartitionActor>()->GetDefaultGridSize(World);

		return UActorPartitionSubsystem::FCellCoord::GetCellCoord(GetParams.LocationHint, World->PersistentLevel, GridSize);
	}

	virtual APartitionActor* GetActor(const TSubclassOf<APartitionActor>& InActorClass, bool bInCreate, const UActorPartitionSubsystem::FCellCoord& InCellCoord)
	{
		APartitionActor* FoundActor = nullptr;
		const uint32 GridSize = InActorClass->GetDefaultObject<APartitionActor>()->GetDefaultGridSize(World);
		FBox CellBounds(UActorPartitionSubsystem::FCellCoord::GetCellBounds(InCellCoord, GridSize));

		TArray<const FWorldPartitionActorDesc*> InstancedObjectsActorDescs = WorldPartition->GetIntersectingActorDescs(CellBounds, InActorClass);

		for (const FWorldPartitionActorDesc* ActorDesc: InstancedObjectsActorDescs)
		{
			if (ActorDesc->GetActorClass() == InActorClass)
			{
				FPartitionActorDesc* PartitionActorDesc = (FPartitionActorDesc*)ActorDesc;
				if ((PartitionActorDesc->GridIndexX == InCellCoord.X) &&
					(PartitionActorDesc->GridIndexY == InCellCoord.Y) &&
					(PartitionActorDesc->GridIndexZ == InCellCoord.Z))
				{
					AActor* DescActor = ActorDesc->GetActor();

					if (!DescActor)
					{
						// Actor exists but is not loaded
						return nullptr;
					}
				
					FoundActor = CastChecked<APartitionActor>(DescActor);
					check(FoundActor->GridSize == GridSize);
				}
			}
		}

		if (!FoundActor && bInCreate)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = InCellCoord.Level;
			SpawnParams.Name = FName(*FString::Printf(TEXT("%s_%d_%d_%d"), *InActorClass->GetName(), InCellCoord.X, InCellCoord.Y, InCellCoord.Z));
			SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
							
			FVector CellCenter(CellBounds.GetCenter());
			FoundActor = CastChecked<APartitionActor>(World->SpawnActor(InActorClass, &CellCenter, nullptr, SpawnParams));
			FoundActor->GridSize = GridSize;
			FoundActor->bLockLocation = true;

			WorldPartition->UpdateActorDesc(FoundActor);
		}

		check(FoundActor || !bInCreate);
		return FoundActor;
	}

private:
	UWorldPartitionSubsystem* WorldPartition;
};

#endif // WITH_EDITOR

UActorPartitionSubsystem::UActorPartitionSubsystem()
{}

bool UActorPartitionSubsystem::IsLevelPartition() const
{
	return !GetWorld()->HasSubsystem<UWorldPartitionSubsystem>();
}

#if WITH_EDITOR
void UActorPartitionSubsystem::InitializeForWorldPartitionConversion()
{
	check(IsRunningCommandlet());
	ActorPartition.Reset(new FActorPartitionWorldPartition(GetWorld()));
}

void UActorPartitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorldPartitionSubsystem* WorldPartitionSubsystem = Collection.InitializeDependency<UWorldPartitionSubsystem>();
	if (WorldPartitionSubsystem)
	{
		WorldPartitionSubsystem->RegisterActorDescFactory(APartitionActor::StaticClass(), &PartitionActorDescFactory);
	}

	// Will need to register to WorldPartition setup changes events here...
	InitializeActorPartition();
}

/** Implement this for deinitialization of instances of the system */
void UActorPartitionSubsystem::Deinitialize()
{
	if (ActorPartition)
	{
		ActorPartition->GetOnActorPartitionHashInvalidated().Remove(ActorPartitionHashInvalidatedHandle);
	}
}

void UActorPartitionSubsystem::OnActorPartitionHashInvalidated(const FCellCoord& Hash)
{
	PartitionedActors.Remove(Hash);
}

void UActorPartitionSubsystem::InitializeActorPartition()
{
	check(!ActorPartition);

	if (IsLevelPartition())
	{
		ActorPartition.Reset(new FActorPartitionLevel(GetWorld()));
	}
	else
	{
		ActorPartition.Reset(new FActorPartitionWorldPartition(GetWorld()));
	}
	ActorPartitionHashInvalidatedHandle = ActorPartition->GetOnActorPartitionHashInvalidated().AddUObject(this, &UActorPartitionSubsystem::OnActorPartitionHashInvalidated);
}

APartitionActor* UActorPartitionSubsystem::GetActor(const FActorPartitionGetParams& GetParams)
{
	FCellCoord CellCoord = ActorPartition->GetActorPartitionHash(GetParams);

	return GetActor(GetParams.ActorClass, CellCoord, GetParams.bCreate);
}

APartitionActor* UActorPartitionSubsystem::GetActor(const TSubclassOf<APartitionActor>& InActorClass, const FCellCoord& InCellCoords, bool bInCreate)
{
	TMap<UClass*, TWeakObjectPtr<APartitionActor>>* ActorsPerClass = PartitionedActors.Find(InCellCoords);
	APartitionActor* FoundActor = nullptr;
	if (!ActorsPerClass)
	{
		FoundActor = ActorPartition->GetActor(InActorClass, bInCreate, InCellCoords);
		if (FoundActor)
		{
			PartitionedActors.Add(InCellCoords).Add(InActorClass, FoundActor);
			return FoundActor;
		}
		else if (bInCreate)
		{
			// Actor wasn't found and couldn't be created
			return nullptr;
		}
	}

	TWeakObjectPtr<APartitionActor>* ActorPtr = ActorsPerClass->Find(InActorClass);
	if (!ActorPtr || !ActorPtr->IsValid())
	{
		FoundActor = ActorPartition->GetActor(InActorClass, bInCreate, InCellCoords);
		if (FoundActor)
		{
			if (!ActorPtr)
			{
				ActorsPerClass->Add(InActorClass, FoundActor);
			}
			else
			{
				*ActorPtr = FoundActor;
			}
		}
	}
	else
	{
		FoundActor = (*ActorPtr).Get();
	}
	
	return FoundActor;
}

#endif // WITH_EDITOR
