// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionEditorSpatialHash.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

#if WITH_EDITOR
TAutoConsoleVariable<bool> CVarEnableSpatialHashValidation(TEXT("wp.Editor.EnableSpatialHashValidation"), false, TEXT("Whether to enable World Partition editor spatial hash validation"), ECVF_Default);
#endif

UWorldPartitionEditorSpatialHash::UWorldPartitionEditorSpatialHash(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, CellSize(12800)
	, bBoundsDirty(false)
#endif
{}

#if WITH_EDITOR
void UWorldPartitionEditorSpatialHash::Initialize()
{
	check(!AlwaysLoadedCell);

	AlwaysLoadedCell = MakeUnique<FCell>();
	AlwaysLoadedCell->Bounds.Init();
}

void UWorldPartitionEditorSpatialHash::SetDefaultValues()
{}

FName UWorldPartitionEditorSpatialHash::GetWorldPartitionEditorName() const
{
	return TEXT("SpatialHash");
}

FBox UWorldPartitionEditorSpatialHash::GetEditorWorldBounds() const
{
	return Bounds;
}

void UWorldPartitionEditorSpatialHash::Tick(float DeltaSeconds)
{
	if (bBoundsDirty)
	{
		FBox NewBounds(ForceInit);
		for (FCell* Cell: Cells)
		{
			NewBounds += Cell->Bounds;
		}

		const int32 OldLevel = GetLevelForBox(Bounds);
		check(OldLevel == HashLevels.Num() - 1);

		const int32 NewLevel = GetLevelForBox(NewBounds);
		check(NewLevel <= OldLevel);		

		if (NewLevel < OldLevel)
		{
			HashLevels.SetNum(NewLevel + 1);
		}

		Bounds = NewBounds;
		bBoundsDirty = false;
	}

	if (CVarEnableSpatialHashValidation.GetValueOnAnyThread())
	{
		if (Bounds.IsValid)
		{
			const int32 CurrentLevel = GetLevelForBox(Bounds);
			check(CurrentLevel == HashLevels.Num() - 1);

			for (int32 HashLevel = 0; HashLevel < HashLevels.Num() - 1; HashLevel++)
			{
				for (auto& HashLevelPair : HashLevels[HashLevel])
				{
					const FCellCoord CellCoord = HashLevelPair.Key;
					check(CellCoord.Level == HashLevel);

					const uint32 ChildIndex = CellCoord.GetChildIndex();
					const FCellCoord ParentCellCoord = CellCoord.GetParentCellCoord();
					check(ParentCellCoord.Level == HashLevel + 1);

					const FCellNodeElement& ParentCellNodeElement = HashLevels[ParentCellCoord.Level].FindChecked(ParentCellCoord);
					const FCellNode& ParentCellNode = ParentCellNodeElement.Key;
					check(ParentCellNode.HasChildNode(ChildIndex));
				}
			}
		}
	}
}

void UWorldPartitionEditorSpatialHash::HashActor(FWorldPartitionHandle& InActorHandle)
{
	check(InActorHandle.IsValid());

	if (!InActorHandle->GetIsSpatiallyLoaded())
	{
		AlwaysLoadedCell->Actors.Add(InActorHandle);
	}
	else
	{
		const FBox ActorBounds = InActorHandle->GetBounds();
		const int32 CurrentLevel = GetLevelForBox(Bounds);
		const int32 ActorLevel = GetLevelForBox(ActorBounds);

		if (HashLevels.Num() <= ActorLevel)
		{
			HashLevels.AddDefaulted(ActorLevel - HashLevels.Num() + 1);
		}

		ForEachIntersectingCells(ActorBounds, ActorLevel, [&](const FCellCoord& CellCoord)
		{
			FCellNodeElement& CellNodeElement = HashLevels[CellCoord.Level].FindOrAdd(CellCoord);

			TUniquePtr<FCell>& Cell = CellNodeElement.Value;

			if (!Cell)
			{
				Cell = MakeUnique<FCell>();
				Cell->Bounds = GetCellBounds(CellCoord);

				Cells.Add(Cell.Get());

				// Increment spatial structure bounds
				Bounds += Cell->Bounds;

				// Update parent nodes
				FCellCoord CurrentCellCoord = CellCoord;
				while (CurrentCellCoord.Level < CurrentLevel)
				{
					const uint32 ChildIndex = CurrentCellCoord.GetChildIndex();
					CurrentCellCoord = CurrentCellCoord.GetParentCellCoord();

					FCellNodeElement& ParentCellNodeElement = HashLevels[CurrentCellCoord.Level].FindOrAdd(CurrentCellCoord);
					FCellNode& ParentCellNode = ParentCellNodeElement.Key;

					if (ParentCellNode.HasChildNode(ChildIndex))
					{
						break;
					}

					ParentCellNode.AddChildNode(ChildIndex);
				}
			}

			check(Cell);
			Cell->Actors.Add(InActorHandle);
		});

		const int32 NewLevel = GetLevelForBox(Bounds);
		check(NewLevel >= CurrentLevel);

		if (NewLevel > CurrentLevel)
		{
			if (HashLevels.Num() <= NewLevel)
			{
				HashLevels.AddDefaulted(NewLevel - HashLevels.Num() + 1);
			}

			for (int32 Level = CurrentLevel; Level < NewLevel; Level++)
			{
				for (auto& HashLevelPair : HashLevels[Level])
				{
					FCellCoord LevelCellCoord = HashLevelPair.Key;
					while (LevelCellCoord.Level < NewLevel)
					{
						const uint32 ChildIndex = LevelCellCoord.GetChildIndex();

						LevelCellCoord = LevelCellCoord.GetParentCellCoord();

						FCellNodeElement& CellNodeElement = HashLevels[LevelCellCoord.Level].FindOrAdd(LevelCellCoord);
						FCellNode& CellNode = CellNodeElement.Key;

						// We can break updating when aggregated flags are already properly set for parent nodes
						const bool bShouldBreak = CellNode.HasChildNodes();

						// Propagate the child mask
						if (!CellNode.HasChildNode(ChildIndex))
						{
							CellNode.AddChildNode(ChildIndex);
						}				
				
						if (bShouldBreak)
						{
							break;
						}
					}
				}
			}
		}
	}
}

void UWorldPartitionEditorSpatialHash::UnhashActor(FWorldPartitionHandle& InActorHandle)
{
	check(InActorHandle.IsValid());

	if (!InActorHandle->GetIsSpatiallyLoaded())
	{
		AlwaysLoadedCell->Actors.Remove(InActorHandle);
	}
	else
	{
		const FBox ActorBounds = InActorHandle->GetBounds();
		const int32 CurrentLevel = GetLevelForBox(Bounds);
		const int32 ActorLevel = GetLevelForBox(ActorBounds);

		ForEachIntersectingCells(ActorBounds, ActorLevel, [&](const FCellCoord& CellCoord)
		{
			FCellNodeElement& CellNodeElement = HashLevels[CellCoord.Level].FindChecked(CellCoord);
			TUniquePtr<FCell>& Cell = CellNodeElement.Value;
			check(Cell);

			Cell->Actors.Remove(InActorHandle);

			if (!Cell->Actors.Num())
			{
				verify(Cells.Remove(Cell.Get()));
				CellNodeElement.Value.Reset();

				if (!CellNodeElement.Key.HasChildNodes())
				{
					FCellCoord CurrentCellCoord = CellCoord;
					while (CurrentCellCoord.Level < CurrentLevel)
					{
						FCellCoord ParentCellCoord = CurrentCellCoord.GetParentCellCoord();
						FCellNodeElement& ParentCellNodeElement = HashLevels[ParentCellCoord.Level].FindChecked(ParentCellCoord);
						FCellNode& ParentCellNode = ParentCellNodeElement.Key;

						const uint32 ChildIndex = CurrentCellCoord.GetChildIndex();
						ParentCellNode.RemoveChildNode(ChildIndex);

						HashLevels[CurrentCellCoord.Level].Remove(CurrentCellCoord);

						if (ParentCellNodeElement.Value || ParentCellNodeElement.Key.HasChildNodes())
						{
							break;
						}

						CurrentCellCoord = ParentCellCoord;
					}
				}

				bBoundsDirty = true;
			}
		});
	}
}

int32 UWorldPartitionEditorSpatialHash::ForEachIntersectingActor(const FBox& Box, TFunctionRef<void(FWorldPartitionActorDesc*)> InOperation, bool bIncludeSpatiallyLoadedActors, bool bIncludeNonSpatiallyLoadedActors)
{
	TSet<FWorldPartitionActorDesc*> IntersectedActorDescs;

	if (bIncludeSpatiallyLoadedActors)
	{
		ForEachIntersectingCell(Box, [&](FCell* Cell)
		{
			for(FWorldPartitionHandle& ActorDesc: Cell->Actors)
			{
				if (ActorDesc.IsValid())
				{
					bool bWasAlreadyInSet;
					IntersectedActorDescs.Add(*ActorDesc, &bWasAlreadyInSet);

					if (!bWasAlreadyInSet)
					{
						if (Box.Intersect(ActorDesc->GetBounds()))
						{
							InOperation(*ActorDesc);
						}
					}
				}
			}
		});
	}

	if (bIncludeNonSpatiallyLoadedActors)
	{
		for(FWorldPartitionHandle& ActorDesc: AlwaysLoadedCell->Actors)
		{
			if (ActorDesc.IsValid())
			{
				if (Box.Intersect(ActorDesc->GetBounds()))
				{
					bool bWasAlreadyInSet;
					IntersectedActorDescs.Add(*ActorDesc, &bWasAlreadyInSet);
				
					if (!bWasAlreadyInSet)
					{
						InOperation(*ActorDesc);
					}
				}
			}
		}
	}

	return IntersectedActorDescs.Num();
}

int32 UWorldPartitionEditorSpatialHash::ForEachIntersectingCellInner(const FBox& Box, const FCellCoord& CellCoord, TFunctionRef<void(FCell*)> InOperation)
{
	int32 NumIntersecting = 0;

	if (const FCellNodeElement* CellNodeElement = HashLevels[CellCoord.Level].Find(CellCoord))
	{
		if (CellNodeElement->Value)
		{
			InOperation(CellNodeElement->Value.Get());
			NumIntersecting++;
		}

		CellNodeElement->Key.ForEachChild([&](uint32 ChildIndex)
		{
			const FCellCoord ChildCellCoord = CellCoord.GetChildCellCoord(ChildIndex);
			const FBox CellBounds = GetCellBounds(ChildCellCoord);

			if (Box.Intersect(CellBounds))
			{
				NumIntersecting += ForEachIntersectingCellInner(Box, ChildCellCoord, InOperation);
			}
		});
	}

	return NumIntersecting;
}

int32 UWorldPartitionEditorSpatialHash::ForEachIntersectingCell(const FBox& Box, TFunctionRef<void(FCell*)> InOperation)
{
	int32 NumIntersecting = 0;

	if (HashLevels.Num())
	{
		const FBox SearchBox = Box.Overlap(Bounds);

		if (SearchBox.IsValid)
		{
			ForEachIntersectingCells(SearchBox, HashLevels.Num() - 1, [&](const FCellCoord& CellCoord)
			{
				NumIntersecting += ForEachIntersectingCellInner(Box, CellCoord, InOperation);
			});
		}
	}

	return NumIntersecting;
}

uint32 UWorldPartitionEditorSpatialHash::GetWantedEditorCellSize() const
{
	return WantedCellSize ? WantedCellSize : CellSize;
}

void UWorldPartitionEditorSpatialHash::SetEditorWantedCellSize(uint32 InCellSize)
{
	Modify();
	WantedCellSize = InCellSize;
}

void UWorldPartitionEditorSpatialHash::PostLoad()
{
	Super::PostLoad();

	if (WantedCellSize && (CellSize != WantedCellSize))
	{
		CellSize = WantedCellSize;
		WantedCellSize = 0;
	}
}
#endif

#undef LOCTEXT_NAMESPACE
