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

	AlwaysLoadedCell = NewObject<UWorldPartitionEditorCell>(this, TEXT("AlwaysLoadedCell"), RF_Transactional | RF_Transient);
	AlwaysLoadedCell->Bounds.Init();
}

void UWorldPartitionEditorSpatialHash::SetDefaultValues()
{
	CellSize = 51200;
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
					HashNodes.Remove(CellCoord);
				});
			}
		}

		Bounds = NewBounds;
		bBoundsDirty = false;
	}
}

// In the editor, actors are always using their bounds for grid placement, which makes more sense from a user standpoint.
FBox UWorldPartitionEditorSpatialHash::GetActorBounds(FWorldPartitionActorDesc* InActorDesc) const
{
	FBox ActorBounds;

	switch(InActorDesc->GetGridPlacement())
	{
	case EActorGridPlacement::Location:
	case EActorGridPlacement::Bounds:
	{
		ActorBounds = InActorDesc->GetBounds();
		break;
	}
	}

	check(ActorBounds.IsValid);
	return ActorBounds;
}

bool UWorldPartitionEditorSpatialHash::IsActorAlwaysLoaded(FWorldPartitionActorDesc* InActorDesc) const
{
	if (InActorDesc->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded)
	{
		return true;
	}

	// If an actor covers more that 4 levels in the octree (which means 32K cells), treat it as always loaded
	const FBox ActorBounds = GetActorBounds(InActorDesc);
	const int32 ActorLevel = GetLevelForBox(ActorBounds);
	return (ActorLevel > 4);
}

void UWorldPartitionEditorSpatialHash::HashActor(FWorldPartitionActorDesc* InActorDesc)
{
	check(InActorDesc);

	if (IsActorAlwaysLoaded(InActorDesc))
	{
		AlwaysLoadedCell->AddActor(InActorDesc);
	}
	else
	{
		const FBox ActorBounds = GetActorBounds(InActorDesc);
		const int32 CurrentLevel = GetLevelForBox(Bounds);

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

		ForEachIntersectingCells(ActorBounds, 0, [&](const FCellCoord& CellCoord)
		{
			UWorldPartitionEditorCell* EditorCell = nullptr;
			if (UWorldPartitionEditorCell** EditorCellPtr = HashCells.Find(CellCoord))
			{
				EditorCell = *EditorCellPtr;
			}
			else
			{
				EditorCell = NewObject<UWorldPartitionEditorCell>(this, *FString::Printf(TEXT("EditorCell_X%lld_Y%lld_Z%lld_L%d"), CellCoord.X, CellCoord.Y, CellCoord.Z, CellCoord.Level), RF_Transient);
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

		const int32 NewLevel = GetLevelForBox(Bounds);
		check(NewLevel >= CurrentLevel);

		if (NewLevel > CurrentLevel)
		{
			ForEachIntersectingCells(Bounds, CurrentLevel, [&](const FCellCoord& CellCoord)
			{
				if (HashCells.Contains(CellCoord))
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

	if (IsActorAlwaysLoaded(InActorDesc))
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
#endif

#undef LOCTEXT_NAMESPACE
