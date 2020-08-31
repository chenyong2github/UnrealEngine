// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorPartition/ActorPartitionSubsystem.h"
#include "ActorPartition/InstancedObjectsActor.h"
#include "WorldPartition/ActorPartition/InstancedObjectsActorDesc.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogActorPartitionSubsystem, All, All);

#if WITH_EDITOR

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
	
	AActor* GetActor(const FActorPartitionGetParams& GetParams, const UActorPartitionSubsystem::FCellCoord& CellCoord) override
	{
		check(GetParams.LevelHint);
		ULevel* SpawnLevel = GetSpawnLevel(GetParams.LevelHint, GetParams.LocationHint);

		AActor* FoundActor = nullptr;
		for (AActor* Actor : SpawnLevel->Actors)
		{
			if (Actor && Actor->IsA(GetParams.ActorClass))
			{
				FoundActor = Actor;
				break;
			}
		}
		
		if (!FoundActor && GetParams.bCreate)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = SpawnLevel;
			SpawnParams.bCreateActorPackage = true;
			FoundActor = World->SpawnActor(GetParams.ActorClass, &GetParams.LocationHint, nullptr, SpawnParams);
		}

		check(FoundActor || !GetParams.bCreate);
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
		WorldSettings = World->GetWorldSettings();
		check(WorldSettings);

		WorldPartition = InWorld->GetSubsystem<UWorldPartitionSubsystem>();
		check(WorldPartition);
	}

	UActorPartitionSubsystem::FCellCoord GetActorPartitionHash(const FActorPartitionGetParams& GetParams) const override 
	{
		return UActorPartitionSubsystem::FCellCoord(
			FMath::FloorToInt(GetParams.LocationHint.X / GetParams.GridSize),
			FMath::FloorToInt(GetParams.LocationHint.Y / GetParams.GridSize),
			FMath::FloorToInt(GetParams.LocationHint.Z / GetParams.GridSize),
			World->PersistentLevel
		);
	}

	virtual AActor* GetActor(const FActorPartitionGetParams& GetParams, const UActorPartitionSubsystem::FCellCoord& CellCoord)
	{
		AActor* FoundActor = nullptr;
		
		FBox CellBox(
			FVector(
				CellCoord.X * GetParams.GridSize, 
				CellCoord.Y * GetParams.GridSize, 
				CellCoord.Z * GetParams.GridSize
			), 
			FVector(
				CellCoord.X * GetParams.GridSize + GetParams.GridSize, 
				CellCoord.Y * GetParams.GridSize + GetParams.GridSize, 
				CellCoord.Z * GetParams.GridSize + GetParams.GridSize
			)
		);

		TArray<const FWorldPartitionActorDesc*> InstancedObjectsActorDescs = WorldPartition->GetIntersectingActorDescs(CellBox, GetParams.ActorClass);

		for (const FWorldPartitionActorDesc* ActorDesc: InstancedObjectsActorDescs)
		{
			if (ActorDesc->GetActorClass() == GetParams.ActorClass)
			{
				FInstancedObjectsActorDesc* InstancedObjectsActorDesc = (FInstancedObjectsActorDesc*)ActorDesc;
				if ((InstancedObjectsActorDesc->GridIndexX == CellCoord.X) && 
					(InstancedObjectsActorDesc->GridIndexY == CellCoord.Y) && 
					(InstancedObjectsActorDesc->GridIndexZ == CellCoord.Z))
				{
					FoundActor = ActorDesc->GetActor();

					if (!FoundActor)
					{
						// Actor exists but is not loaded
						return nullptr;
					}
				
					AInstancedObjectsActor* InstancedObjectsActor = CastChecked<AInstancedObjectsActor>(FoundActor);
					check(InstancedObjectsActor->GridSize == GetParams.GridSize);
				}
			}
		}

		if (!FoundActor && GetParams.bCreate)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.bCreateActorPackage = true;
			FVector CellCenter(CellBox.GetCenter());
			FoundActor = World->SpawnActor(GetParams.ActorClass, &CellCenter, nullptr, SpawnParams);

			AInstancedObjectsActor* InstancedObjectsActor = CastChecked<AInstancedObjectsActor>(FoundActor);
			InstancedObjectsActor->GridSize = GetParams.GridSize;
		}

		check(FoundActor || !GetParams.bCreate);
		return FoundActor;
	}

private:
	AWorldSettings* WorldSettings;
	UWorldPartitionSubsystem* WorldPartition;
};

#endif // WITH_EDITOR

UActorPartitionSubsystem::UActorPartitionSubsystem()
{}

bool UActorPartitionSubsystem::IsLevelPartition() const
{
	UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
	return WorldPartitionSubsystem == nullptr || !WorldPartitionSubsystem->IsEnabled();
}

#if WITH_EDITOR
void UActorPartitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency(UWorldPartitionSubsystem::StaticClass());

	if (UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>())
	{
		WorldPartitionSubsystem->RegisterActorDescFactory(AInstancedObjectsActor::StaticClass(), &InstancedObjectsActorDescFactory);
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

AActor* UActorPartitionSubsystem::GetActor(const FActorPartitionGetParams& GetParams)
{
	check(GetParams.ActorClass->IsChildOf(AInstancedObjectsActor::StaticClass()));

	FCellCoord CellCoord = ActorPartition->GetActorPartitionHash(GetParams);
		
	TMap<UClass*, TWeakObjectPtr<AActor>>* ActorsPerClass = PartitionedActors.Find(CellCoord);
	AActor* FoundActor = nullptr;
	if (!ActorsPerClass)
	{
		FoundActor = ActorPartition->GetActor(GetParams, CellCoord);
		if (FoundActor)
		{
			PartitionedActors.Add(CellCoord).Add(GetParams.ActorClass, FoundActor);
			return FoundActor;
		}
		else if (GetParams.bCreate)
		{
			// Actor wasn't found and couldn't be created
			return nullptr;
		}
	}

	TWeakObjectPtr<AActor>* ActorPtr = ActorsPerClass->Find(GetParams.ActorClass);
	if (!ActorPtr || !ActorPtr->IsValid())
	{
		FoundActor = ActorPartition->GetActor(GetParams, CellCoord);
		if (FoundActor)
		{
			if (!ActorPtr)
			{
				ActorsPerClass->Add(GetParams.ActorClass, FoundActor);
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
