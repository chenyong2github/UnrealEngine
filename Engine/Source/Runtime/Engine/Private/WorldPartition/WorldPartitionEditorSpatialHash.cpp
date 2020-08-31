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
					FCellNode CellNode;
					if (HashCells.RemoveAndCopyValue(CellCoord, CellNode))
					{
						check(!CellNode.Cell);
					}
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

void UWorldPartitionEditorSpatialHash::HashActor(FWorldPartitionActorDesc* InActorDesc)
{
	check(InActorDesc);
	if (InActorDesc->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded)
	{
		AlwaysLoadedCell->AddActor(InActorDesc);
	}
	else
	{
		const FBox ActorBounds = GetActorBounds(InActorDesc);
		const int32 CurrentLevel = GetLevelForBox(Bounds);
		const int32 ActorLevel = GetLevelForBox(ActorBounds);

		auto UpdateHigherLevels = [this](const FCellCoord& CellCoord, int32 EndLevel)
		{
			FCellCoord LevelCellCoord = CellCoord;
			for (int32 Level=CellCoord.Level+1; Level<=EndLevel; Level++)
			{
				const uint32 ChildIndex = LevelCellCoord.GetChildIndex();

				LevelCellCoord = LevelCellCoord.GetParentCellCoord();

				FCellNode& CellNode = HashCells.FindOrAdd(LevelCellCoord);

				if (CellNode.HasChildNode(ChildIndex))
				{
					break;
				}

				CellNode.AddChildNode(ChildIndex);
			}
		};

		ForEachIntersectingCells(ActorBounds, ActorLevel, [&](const FCellCoord& CellCoord)
		{
			FCellNode& CellNode = HashCells.FindOrAdd(CellCoord);

			if (!CellNode.Cell)
			{
				CellNode.Cell = NewObject<UWorldPartitionEditorCell>(this, *FString::Printf(TEXT("EditorCell_X%lld_Y%lld_Z%lld_L%d"), CellCoord.X, CellCoord.Y, CellCoord.Z, CellCoord.Level), RF_Transient);
				CellNode.Cell->SetFlags(RF_Transactional);
				CellNode.Cell->Bounds = GetCellBounds(CellCoord);

				Cells.Add(CellNode.Cell);

				UpdateHigherLevels(CellCoord, CurrentLevel);

				Bounds += CellNode.Cell->Bounds;
			}

			CellNode.Cell->AddActor(InActorDesc);
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
	if (InActorDesc->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded)
	{
		AlwaysLoadedCell->RemoveActor(InActorDesc);
	}
	else
	{
		const FBox ActorBounds = GetActorBounds(InActorDesc);
		const int32 CurrentLevel = GetLevelForBox(Bounds);
		const int32 ActorLevel = GetLevelForBox(ActorBounds);

		ForEachIntersectingCells(ActorBounds, ActorLevel, [&](const FCellCoord& CellCoord)
		{
			FCellNode& CellNode = HashCells.FindChecked(CellCoord);

			CellNode.Cell->RemoveActor(InActorDesc);

			if (!CellNode.Cell->Actors.Num())
			{
				verify(Cells.Remove(CellNode.Cell));
				CellNode.Cell = nullptr;

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

	FCellNode CellNode;
	if (FCellNode* CellNodePtr = HashCells.Find(CellCoord))
	{
		CellNode = *CellNodePtr;

		if (CellNode.Cell)
		{
			InOperation(CellNode.Cell);
			NumIntersecting++;
		}

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

	return NumIntersecting;
}

int32 UWorldPartitionEditorSpatialHash::ForEachIntersectingCell(const FBox& Box, TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation)
{
	int32 NumIntersecting = 0;

	const int32 SearchLevel = GetLevelForBox(Bounds);
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
