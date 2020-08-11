// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorPartition/ActorPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionEditorCell.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogActorPartitionSubsystem, All, All);

#if WITH_EDITOR

/**
 * FActorPartitionLevel
 */
class FActorPartitionLevel : public FBaseActorPartition
{
public:
	FActorPartitionLevel(UWorld* InWorld) : FBaseActorPartition(InWorld) 
	{
		 LevelRemovedFromWorldHandle = FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FActorPartitionLevel::OnLevelRemovedFromWorld);
	}

	~FActorPartitionLevel()
	{
		FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedFromWorldHandle);
	}

	bool GetActorPartitionHash(const FActorPartitionGetParams& GetParams, FActorPartitionHash& OutPartitionHash) const override 
	{ 
		ULevel* SpawnLevel = GetSpawnLevel(GetParams.LevelHint, GetParams.LocationHint);
		OutPartitionHash = GetLevelHash(SpawnLevel);
		return true;
	}
	
	AActor* GetActor(const FActorPartitionGetParams& GetParams) override
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
		if (InWorld != World)
		{
			return;
		}

		FActorPartitionHash LevelHash = GetLevelHash(InLevel);
		GetOnActorPartitionHashInvalidated().Broadcast(LevelHash);
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

	FActorPartitionHash GetLevelHash(ULevel* Level) const
	{
		return StaticCast<FActorPartitionHash>(FCrc::MemCrc32(&Level, sizeof(ULevel*)));
	}

	FDelegateHandle LevelRemovedFromWorldHandle;
};

/**
 * FActorPartitionWorldPartition
 */
class FActorPartitionWorldPartition : public FBaseActorPartition
{
public:
	FActorPartitionWorldPartition(UWorld* InWorld) : FBaseActorPartition(InWorld)
	{
		WorldPartition = InWorld->GetSubsystem<UWorldPartitionSubsystem>();
		check(WorldPartition);
	}

	bool GetActorPartitionHash(const FActorPartitionGetParams& GetParams, FActorPartitionHash& OutPartitionHash) const override
	{ 
		FVector CellCenter;
		UWorldPartitionEditorCell* Cell = nullptr;
		if (!WorldPartition->GetCellAtLocation(GetParams.LocationHint, CellCenter, Cell))
		{
			return false;
		}

		if (!Cell->bLoaded)
		{
			return false;
		}

		OutPartitionHash = StaticCast<FActorPartitionHash>(FCrc::MemCrc32(&CellCenter, sizeof(CellCenter)));
		return true;
	}

	virtual AActor* GetActor(const FActorPartitionGetParams& GetParams)
	{
		AActor* FoundActor = nullptr;
		FVector CellCenter;
		UWorldPartitionEditorCell* Cell = nullptr;
		if (!WorldPartition->GetCellAtLocation(GetParams.LocationHint, CellCenter, Cell))
		{
			return nullptr;
		}

		if (!Cell->bLoaded)
		{
			return nullptr;
		}

		TArray<AActor*> CellActors;
		CellActors.Reserve(Cell->Actors.Num());
		WorldPartition->GetCellActors(Cell, CellActors);
		for (AActor* Actor : CellActors)
		{
			if (Actor->IsA(GetParams.ActorClass))
			{
				FoundActor = Actor;
				break;
			}
		}

		if (!FoundActor && GetParams.bCreate)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.bCreateActorPackage = true;
			FoundActor = World->SpawnActor(GetParams.ActorClass, &CellCenter, nullptr, SpawnParams);

			if (GetParams.ActorClass->GetDefaultObject<AActor>()->GetDefaultGridPlacement() != EActorGridPlacement::Location)
			{
				UE_LOG(LogActorPartitionSubsystem, All, TEXT("Spawned non location-based partition actor %s"), *FoundActor->GetName());
			}
		}

		check(FoundActor || !GetParams.bCreate);
		return FoundActor;
	}

private:
	UWorldPartitionSubsystem* WorldPartition;
};

#endif // WITH_EDITOR

UActorPartitionSubsystem::UActorPartitionSubsystem()
{

}

bool UActorPartitionSubsystem::IsLevelPartition() const
{
	UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
	return WorldPartitionSubsystem == nullptr || !WorldPartitionSubsystem->IsEnabled();
}

#if WITH_EDITOR
void UActorPartitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency(UWorldPartitionSubsystem::StaticClass());

	// Will need to register to WorldPartition setup changes events here...
	InitializeActorPartition();
}

/** Implement this for deinitialization of instances of the system */
void UActorPartitionSubsystem::Deinitialize()
{
	if (ActorPartitionPtr)
	{
		ActorPartitionPtr->GetOnActorPartitionHashInvalidated().Remove(ActorPartitionHashInvalidatedHandle);
	}
}

void UActorPartitionSubsystem::OnActorPartitionHashInvalidated(const FActorPartitionHash& Hash)
{
	PartitionedActors.Remove(Hash);
}

void UActorPartitionSubsystem::InitializeActorPartition()
{
	check(!ActorPartitionPtr);

	if (IsLevelPartition())
	{
		ActorPartitionPtr.Reset(new FActorPartitionLevel(GetWorld()));
	}
	else
	{
		ActorPartitionPtr.Reset(new FActorPartitionWorldPartition(GetWorld()));
	}
	ActorPartitionHashInvalidatedHandle = ActorPartitionPtr->GetOnActorPartitionHashInvalidated().AddUObject(this, &UActorPartitionSubsystem::OnActorPartitionHashInvalidated);
}

FBaseActorPartition& UActorPartitionSubsystem::GetActorPartition()
{
	check(ActorPartitionPtr);
	return *ActorPartitionPtr;
}

AActor* UActorPartitionSubsystem::GetActor(const FActorPartitionGetParams& GetParams)
{
	FBaseActorPartition& ActorPartition = GetActorPartition();

	FActorPartitionHash ActorPartitionHash;
	if (!ActorPartition.GetActorPartitionHash(GetParams, ActorPartitionHash))
	{
		return nullptr;
	}
		
	TMap<UClass*, TWeakObjectPtr<AActor>>* ActorsPerClass = PartitionedActors.Find(ActorPartitionHash);
	AActor* FoundActor = nullptr;
	if (!ActorsPerClass)
	{
		FoundActor = ActorPartition.GetActor(GetParams);
		if (FoundActor)
		{
			PartitionedActors.Add(ActorPartitionHash).Add(GetParams.ActorClass, FoundActor);
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
		FoundActor = ActorPartition.GetActor(GetParams);
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
