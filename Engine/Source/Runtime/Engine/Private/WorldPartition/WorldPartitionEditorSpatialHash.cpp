// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionEditorSpatialHash.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorCell.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

UWorldPartitionEditorSpatialHash::UWorldPartitionEditorSpatialHash(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, bBoundsDirty(false)
	, AlwaysLoadedCell(nullptr)
#endif
{}

#if WITH_EDITOR
void UWorldPartitionEditorSpatialHash::Initialize()
{
	check(!AlwaysLoadedCell);

	AlwaysLoadedCell = NewObject<UWorldPartitionEditorCell>(this, NAME_None, RF_Transactional | RF_Transient);
	AlwaysLoadedCell->Bounds.Init();
}

void UWorldPartitionEditorSpatialHash::SetDefaultValues()
{
	CellSize = 32768;
}

FName UWorldPartitionEditorSpatialHash::GetWorldPartitionEditorName()
{
	return TEXT("SpatialHash");
}

void UWorldPartitionEditorSpatialHash::Tick(float DeltaSeconds)
{
	if (bBoundsDirty)
	{
		FBox NewBounds(ForceInit);
		ForEachCell([&](UWorldPartitionEditorCell* Cell) { NewBounds += Cell->Bounds; });

		const int32 OldLevel = GetLevelForBox(Bounds);
		const int32 NewLevel = GetLevelForBox(NewBounds);
		check(NewLevel <= OldLevel);

		if (NewLevel < OldLevel)
		{
			for (int32 Level=NewLevel+1; Level<=OldLevel; Level++)
			{
				ForEachIntersectingCells(Bounds, Level, [&](const FCellCoord& CellCoord)
				{
					if (HashNodes.Contains(CellCoord))
					{
						HashNodes.Remove(CellCoord);
					}
				});
			}
		}

		Bounds = NewBounds;
		bBoundsDirty = false;
	}
}

void UWorldPartitionEditorSpatialHash::HashActor(FWorldPartitionActorDesc* InActorDesc)
{
	check(InActorDesc);
	if (InActorDesc->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded)
	{
		AlwaysLoadedCell->AddActor(InActorDesc);
	}
	else
	{
		int32 CurrentLevel = GetLevelForBox(Bounds);

		auto UpdateHigherLevels = [this](const FCellCoord& CellCoord, int32 EndLevel)
		{
			FCellCoord LevelCellCoord = CellCoord;
			for (int32 Level=CellCoord.Level+1; Level<=EndLevel; Level++)
			{
				const uint32 ChildIndex = LevelCellCoord.GetChildIndex();

				LevelCellCoord = LevelCellCoord.GetParentCellCoord();

				FCellNode& CellNode = HashNodes.FindOrAdd(LevelCellCoord);

				if (CellNode.HasChildNode(ChildIndex))
				{
					break;
				}

				CellNode.AddChildNode(ChildIndex);
			}
		};

		ForEachIntersectingCells(InActorDesc->GetBounds(), 0, [&](const FCellCoord& CellCoord)
		{
			UWorldPartitionEditorCell* EditorCell = nullptr;
			if (UWorldPartitionEditorCell** EditorCellPtr = HashCells.Find(CellCoord))
			{
				EditorCell = *EditorCellPtr;
			}
			else
			{
				EditorCell = NewObject<UWorldPartitionEditorCell>(this, NAME_None, RF_Transient);
				EditorCell->SetFlags(RF_Transactional);
				EditorCell->Bounds = GetCellBounds(CellCoord);

				Cells.Add(EditorCell);
				HashCells.Add(CellCoord, EditorCell);

				UpdateHigherLevels(CellCoord, CurrentLevel);

				Bounds += EditorCell->Bounds;
			}

			check(EditorCell);
			EditorCell->AddActor(InActorDesc);
		});

		int32 NewLevel = GetLevelForBox(Bounds);
		check(NewLevel >= CurrentLevel);

		if (NewLevel > CurrentLevel)
		{
			ForEachIntersectingCells(Bounds, CurrentLevel, [&](const FCellCoord& CellCoord)
			{
				if (CurrentLevel ? HashNodes.Contains(CellCoord) : HashCells.Contains(CellCoord))
				{
					UpdateHigherLevels(CellCoord, NewLevel);
				}
			});
		}
	}
}

void UWorldPartitionEditorSpatialHash::UnhashActor(FWorldPartitionActorDesc* InActorDesc)
{
	check(InActorDesc);
	if (InActorDesc->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded)
	{
		AlwaysLoadedCell->RemoveActor(InActorDesc);
	}
	else
	{
		int32 CurrentLevel = GetLevelForBox(Bounds);

		ForEachIntersectingCells(InActorDesc->GetBounds(), 0, [&](const FCellCoord& CellCoord)
		{
			UWorldPartitionEditorCell* EditorCell = HashCells.FindChecked(CellCoord);

			EditorCell->RemoveActor(InActorDesc);

			if (!EditorCell->Actors.Num())
			{
				verify(Cells.Remove(EditorCell));
				verify(HashCells.Remove(CellCoord));

				FCellCoord LevelCellCoord = CellCoord;
				for (int32 Level=1; Level<=CurrentLevel; Level++)
				{
					const uint32 ChildIndex = LevelCellCoord.GetChildIndex();

					LevelCellCoord = LevelCellCoord.GetParentCellCoord();

					FCellNode& CellNode = HashNodes.FindChecked(LevelCellCoord);

					CellNode.RemoveChildNode(ChildIndex);

					if (CellNode.HasChildNodes())
					{
						break;
					}

					HashNodes.Remove(LevelCellCoord);
				}

				bBoundsDirty = true;
			}
		});
	}
}

int32 UWorldPartitionEditorSpatialHash::ForEachIntersectingActor(const FBox& Box, TFunctionRef<void(FWorldPartitionActorDesc*)> InOperation)
{
	int32 NumIntersecting = 0;

	FWorldPartitionActorDesc::GlobalTag++;

	ForEachIntersectingCell(Box, [&](UWorldPartitionEditorCell* EditorCell)
	{
		for(FWorldPartitionActorDesc* ActorDesc: EditorCell->Actors)
		{
			if (ActorDesc->Tag != FWorldPartitionActorDesc::GlobalTag)
			{
				if (Box.Intersect(ActorDesc->GetBounds()))
				{
					InOperation(ActorDesc);
					NumIntersecting++;
				}

				ActorDesc->Tag = FWorldPartitionActorDesc::GlobalTag;
			}
		}
	});

	for(FWorldPartitionActorDesc* ActorDesc: AlwaysLoadedCell->Actors)
	{
		check(ActorDesc->Tag != FWorldPartitionActorDesc::GlobalTag);

		if (Box.Intersect(ActorDesc->GetBounds()))
		{
			InOperation(ActorDesc);
			NumIntersecting++;
		}

		ActorDesc->Tag = FWorldPartitionActorDesc::GlobalTag;		
	}

	return NumIntersecting;
}

int32 UWorldPartitionEditorSpatialHash::ForEachIntersectingCellInner(const FBox& Box, const FCellCoord& CellCoord, TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation)
{
	int32 NumIntersecting = 0;

	if (CellCoord.Level)
	{
		FCellNode CellNode;
		if (FCellNode* CellNodePtr = HashNodes.Find(CellCoord))
		{
			CellNode = *CellNodePtr;

			check(CellNode.HasChildNodes());

			CellNode.ForEachChild([&](uint32 ChildIndex)
			{
				const FCellCoord ChildCellCoord = CellCoord.GetChildCellCoord(ChildIndex);
				const FBox CellBounds = GetCellBounds(ChildCellCoord);

				if (Box.Intersect(CellBounds))
				{
					NumIntersecting += ForEachIntersectingCellInner(Box, ChildCellCoord, InOperation);
				}
			});
		}
	}
	else
	{
		UWorldPartitionEditorCell* EditorCell = nullptr;
		if (UWorldPartitionEditorCell** EditorCellPtr = HashCells.Find(CellCoord))
		{
			InOperation(*EditorCellPtr);
			NumIntersecting++;
		}
	}

	return NumIntersecting;
}

int32 UWorldPartitionEditorSpatialHash::ForEachIntersectingCell(const FBox& Box, TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation)
{
	int32 NumIntersecting = 0;

	const FBox SearchBox = Box.Overlap(Bounds);
	const int32 SearchLevel = GetLevelForBox(SearchBox);

	ForEachIntersectingCells(Box, SearchLevel, [&](const FCellCoord& CellCoord)
	{
		NumIntersecting += ForEachIntersectingCellInner(Box, CellCoord, InOperation);
	});

	return NumIntersecting;
}

int32 UWorldPartitionEditorSpatialHash::ForEachCell(TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation)
{
	for (UWorldPartitionEditorCell* Cell: Cells)
	{
		InOperation(Cell);
	}
	return Cells.Num();
}

UWorldPartitionEditorCell* UWorldPartitionEditorSpatialHash::GetAlwaysLoadedCell()
{
	return AlwaysLoadedCell;
}

bool UWorldPartitionEditorSpatialHash::GetCellAtLocation(const FVector& Location, FVector& Center, UWorldPartitionEditorCell*& Cell) const
{
	FCellCoord CellCoord(GetCellCoords(Location, 0));
	if (UWorldPartitionEditorCell* const* CellPtr = (UWorldPartitionEditorCell* const*)HashCells.Find(CellCoord))
	{
		Cell = *CellPtr;
		Center = Cell->Bounds.GetCenter();
		return true;
	}

	return false;
}
#endif

#undef LOCTEXT_NAMESPACE
