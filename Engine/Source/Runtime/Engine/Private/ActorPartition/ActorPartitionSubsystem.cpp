// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorPartition/ActorPartitionSubsystem.h"
#include "ActorPartition/PartitionActor.h"
#include "WorldPartition/ActorPartition/PartitionActorDesc.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogActorPartitionSubsystem, All, All);

#if WITH_EDITOR

FActorPartitionGetParams::FActorPartitionGetParams(const TSubclassOf<APartitionActor>& InActorClass, bool bInCreate, ULevel* InLevelHint, const FVector& InLocationHint, uint32 InGridSize, const FGuid& InGuidHint)
	: ActorClass(InActorClass)
	, bCreate(bInCreate)
	, LocationHint(InLocationHint)
	, LevelHint(InLevelHint)
	, GuidHint(InGuidHint)
	, GridSize(InGridSize)
{
}

void FActorPartitionGridHelper::ForEachIntersectingCell(const TSubclassOf<APartitionActor>& InActorClass, const FBox& InBounds, ULevel* InLevel, TFunctionRef<bool(const UActorPartitionSubsystem::FCellCoord&, const FBox&)> InOperation, uint32 InGridSize)
{
	const uint32 GridSize = InGridSize > 0 ? InGridSize : InActorClass->GetDefaultObject<APartitionActor>()->GetDefaultGridSize(InLevel->GetWorld());
	const UActorPartitionSubsystem::FCellCoord MinCellCoords = UActorPartitionSubsystem::FCellCoord::GetCellCoord(InBounds.Min, InLevel, GridSize);
	const UActorPartitionSubsystem::FCellCoord MaxCellCoords = UActorPartitionSubsystem::FCellCoord::GetCellCoord(InBounds.Max, InLevel, GridSize);

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

void FActorPartitionGridHelper::ForEachIntersectingCell(const TSubclassOf<APartitionActor>& InActorClass, const FIntRect& InRect, ULevel* InLevel, TFunctionRef<bool(const UActorPartitionSubsystem::FCellCoord&, const FIntRect&)> InOperation, uint32 InGridSize)
{
	const uint32 GridSize = InGridSize > 0 ? InGridSize : InActorClass->GetDefaultObject<APartitionActor>()->GetDefaultGridSize(InLevel->GetWorld());
	const UActorPartitionSubsystem::FCellCoord MinCellCoords = UActorPartitionSubsystem::FCellCoord::GetCellCoord(InRect.Min, InLevel, GridSize);
	const UActorPartitionSubsystem::FCellCoord MaxCellCoords = UActorPartitionSubsystem::FCellCoord::GetCellCoord(InRect.Max, InLevel, GridSize);

	for (int32 y = MinCellCoords.Y; y <= MaxCellCoords.Y; y++)
	{
		for (int32 x = MinCellCoords.X; x <= MaxCellCoords.X; x++)
		{
			UActorPartitionSubsystem::FCellCoord CellCoords(x, y, 0, InLevel);
			const FIntPoint Min = FIntPoint(CellCoords.X * GridSize, CellCoords.Y * GridSize);
			const FIntPoint Max = Min + FIntPoint(GridSize);
			FIntRect CellBounds(Min, Max);

			if (!InOperation(MoveTemp(CellCoords), MoveTemp(CellBounds)))
			{
				return;
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
	
	APartitionActor* GetActor(const TSubclassOf<APartitionActor>& InActorClass, bool bInCreate, const UActorPartitionSubsystem::FCellCoord& InCellCoord, const FGuid& InGuid, uint32 InGridSize, bool bInBoundsSearch, TFunctionRef<void(APartitionActor*)> InActorCreated) override
	{
		check(InCellCoord.Level);
		
		APartitionActor* FoundActor = nullptr;
		for (AActor* Actor : InCellCoord.Level->Actors)
		{
			if (APartitionActor* PartitionActor = Cast<APartitionActor>(Actor))
			{
				if (PartitionActor->GetGridGuid() == InGuid)
				{
					FoundActor = PartitionActor;
					break;
				}
			}
		}
		
		if (!FoundActor && bInCreate)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = InCellCoord.Level;
			FoundActor = CastChecked<APartitionActor>(World->SpawnActor(InActorClass, nullptr, nullptr, SpawnParams));
			InActorCreated(FoundActor);
		}

		check(FoundActor || !bInCreate);
		return FoundActor;
	}


	void ForEachRelevantActor(const TSubclassOf<APartitionActor>& InActorClass, const FBox& IntersectionBounds, TFunctionRef<bool(APartitionActor*)>InOperation) const override
	{
		for (TActorIterator<APartitionActor> It(World, InActorClass); It; ++It)
		{
			if (!InOperation(*It))
			{
				return;
			}
		}
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
		check(WorldPartition);
	}

	UActorPartitionSubsystem::FCellCoord GetActorPartitionHash(const FActorPartitionGetParams& GetParams) const override 
	{
		uint32 GridSize = GetParams.GridSize > 0 ? GetParams.GridSize : GetParams.ActorClass->GetDefaultObject<APartitionActor>()->GetDefaultGridSize(World);

		return UActorPartitionSubsystem::FCellCoord::GetCellCoord(GetParams.LocationHint, World->PersistentLevel, GridSize);
	}

	virtual APartitionActor* GetActor(const TSubclassOf<APartitionActor>& InActorClass, bool bInCreate, const UActorPartitionSubsystem::FCellCoord& InCellCoord, const FGuid& InGuid, uint32 InGridSize, bool bInBoundsSearch, TFunctionRef<void(APartitionActor*)> InActorCreated)
	{
		APartitionActor* FoundActor = nullptr;
		bool bUnloadedActorExists = false;
		auto FindActor = [&FoundActor, &bUnloadedActorExists, InActorClass, InCellCoord, InGuid, InGridSize](const FWorldPartitionActorDesc* ActorDesc)
		{
			check(ActorDesc->GetActorClass()->IsChildOf(InActorClass));
			FPartitionActorDesc* PartitionActorDesc = (FPartitionActorDesc*)ActorDesc;
			if ((PartitionActorDesc->GridIndexX == InCellCoord.X) &&
				(PartitionActorDesc->GridIndexY == InCellCoord.Y) &&
				(PartitionActorDesc->GridIndexZ == InCellCoord.Z) &&
				(PartitionActorDesc->GridSize == InGridSize) &&
				(PartitionActorDesc->GridGuid == InGuid))
			{
				AActor* DescActor = ActorDesc->GetActor();

				if (!DescActor)
				{
					// Actor exists but is not loaded
					bUnloadedActorExists = true;
					return false;
				}

				FoundActor = CastChecked<APartitionActor>(DescActor);
				check(FoundActor->GridSize == InGridSize && FoundActor->GetGridGuid() == InGuid);
				return false;
			}
			return true;
		};

		FBox CellBounds = UActorPartitionSubsystem::FCellCoord::GetCellBounds(InCellCoord, InGridSize);
		if (bInBoundsSearch)
		{
			WorldPartition->ForEachIntersectingActorDesc(CellBounds, InActorClass, FindActor);
		}
		else
		{
			WorldPartition->ForEachActorDesc(InActorClass, FindActor);
		}
				
		if (bUnloadedActorExists)
		{
			return nullptr;
		}
				
		if (!FoundActor && bInCreate)
		{
			TStringBuilderWithBuffer<TCHAR, NAME_SIZE> ActorNameBuilder;

			ActorNameBuilder += InActorClass->GetName();
			ActorNameBuilder += TEXT("_");

			if (InGuid.IsValid())
			{
				ActorNameBuilder += InGuid.ToString(EGuidFormats::Base36Encoded);
				ActorNameBuilder += TEXT("_");
			}

			ActorNameBuilder += FString::Printf(TEXT("%d_%d_%d"), InCellCoord.X, InCellCoord.Y, InCellCoord.Z);

			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = InCellCoord.Level;
			SpawnParams.Name = *ActorNameBuilder;
			SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;

			// Handle the case where the actor already exists, but is in the undo stack
			if (UObject* ExistingObject = StaticFindObject(nullptr, World->PersistentLevel, *SpawnParams.Name.ToString()))
			{
				check(CastChecked<AActor>(ExistingObject)->IsPendingKill());
				ExistingObject->Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional | REN_ForceNoResetLoaders);
			}
						
			FVector CellCenter(CellBounds.GetCenter());
			FoundActor = CastChecked<APartitionActor>(World->SpawnActor(InActorClass, &CellCenter, nullptr, SpawnParams));
			FoundActor->GridSize = InGridSize;
			FoundActor->bLockLocation = true;
			
			InActorCreated(FoundActor);
		}

		check(FoundActor || !bInCreate);
		return FoundActor;
	}

	void ForEachRelevantActor(const TSubclassOf<APartitionActor>& InActorClass, const FBox& IntersectionBounds, TFunctionRef<bool(APartitionActor*)>InOperation) const override
	{
		UActorPartitionSubsystem* ActorSubsystem = World->GetSubsystem<UActorPartitionSubsystem>();
		FActorPartitionGridHelper::ForEachIntersectingCell(InActorClass, IntersectionBounds, World->PersistentLevel, [&ActorSubsystem, &InActorClass, &IntersectionBounds, &InOperation](const UActorPartitionSubsystem::FCellCoord& InCellCoord, const FBox& InCellBounds) {

			if (InCellBounds.Intersect(IntersectionBounds))
			{
				const bool bCreate = false;
				if (auto PartitionActor = ActorSubsystem->GetActor(InActorClass, InCellCoord, bCreate))
				{
					return InOperation(PartitionActor);
				}
			}
			return true;
			});
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
void UActorPartitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UWorldPartitionSubsystem>();
	
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

void UActorPartitionSubsystem::ForEachRelevantActor(const TSubclassOf<APartitionActor>& InActorClass, const FBox& IntersectionBounds, TFunctionRef<bool(APartitionActor*)>InOperation) const
{
	if (ActorPartition)
	{
		ActorPartition->ForEachRelevantActor(InActorClass, IntersectionBounds, InOperation);
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
	return GetActor(GetParams.ActorClass, CellCoord, GetParams.bCreate, GetParams.GuidHint, GetParams.GridSize);
}

APartitionActor* UActorPartitionSubsystem::GetActor(const TSubclassOf<APartitionActor>& InActorClass, const FCellCoord& InCellCoords, bool bInCreate, const FGuid& InGuid, uint32 InGridSize, bool bInBoundsSearch, TFunctionRef<void(APartitionActor*)> InActorCreated)
{
	const uint32 GridSize = InGridSize > 0 ? InGridSize : InActorClass->GetDefaultObject<APartitionActor>()->GetDefaultGridSize(GetWorld());
	
	TMap<UClass*, TMap<FGuid, TWeakObjectPtr<APartitionActor>>>* ActorsPerClass = PartitionedActors.Find(InCellCoords);
	APartitionActor* FoundActor = nullptr;
	if (!ActorsPerClass)
	{
		FoundActor = ActorPartition->GetActor(InActorClass, bInCreate, InCellCoords, InGuid, GridSize, bInBoundsSearch, InActorCreated);
		if (FoundActor)
		{
			PartitionedActors.Add(InCellCoords).Add(InActorClass).Add(InGuid, FoundActor);
		}
	}
	else
	{
		TMap<FGuid, TWeakObjectPtr<APartitionActor>>* ActorsPerGuid = ActorsPerClass->Find(InActorClass);
		if (!ActorsPerGuid)
		{
			FoundActor = ActorPartition->GetActor(InActorClass, bInCreate, InCellCoords, InGuid, GridSize, bInBoundsSearch, InActorCreated);
			if (FoundActor)
			{
				ActorsPerClass->Add(InActorClass).Add(InGuid, FoundActor);
			}
		}
		else
		{
			TWeakObjectPtr<APartitionActor>* ActorPtr = ActorsPerGuid->Find(InGuid);
			if (!ActorPtr || !ActorPtr->IsValid())
			{
				FoundActor = ActorPartition->GetActor(InActorClass, bInCreate, InCellCoords, InGuid, GridSize, bInBoundsSearch, InActorCreated);
				if (FoundActor)
				{
					if (!ActorPtr)
					{
						ActorsPerGuid->Add(InGuid, FoundActor);
					}
					else
					{
						*ActorPtr = FoundActor;
					}
				}
			}
			else
			{
				FoundActor = ActorPtr->Get();
			}
		}

	}
	
	return FoundActor;
}

#endif // WITH_EDITOR
