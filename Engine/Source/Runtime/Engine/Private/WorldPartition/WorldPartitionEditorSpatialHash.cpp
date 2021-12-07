// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionEditorSpatialHash.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorCell.h"
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
	, AlwaysLoadedCell(nullptr)
#endif
{}

#if WITH_EDITOR
void UWorldPartitionEditorSpatialHash::Initialize()
{
	check(!AlwaysLoadedCell);

	AlwaysLoadedCell = NewObject<UWorldPartitionEditorCell>(this, TEXT("AlwaysLoadedCell"), RF_Transient);
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

	if (CVarEnableSpatialHashValidation.GetValueOnAnyThread())
	{	
		const int32 CurrentLevel = GetLevelForBox(Bounds);
		ForEachIntersectingCells(Bounds, 0, [&](const FCellCoord& CellCoord)
		{
			UWorldPartitionEditorCell* EditorCell = nullptr;
			if (UWorldPartitionEditorCell** EditorCellPtr = HashCells.Find(CellCoord))
			{
				if ((*EditorCellPtr)->IsLoaded())
				{
					FCellCoord LevelCellCoord = CellCoord;
					while (LevelCellCoord.Level < CurrentLevel)
					{
						const uint32 ChildIndex = LevelCellCoord.GetChildIndex();
						LevelCellCoord = LevelCellCoord.GetParentCellCoord();
						const FCellNode& CellNode = HashNodes.FindChecked(LevelCellCoord);
						check(CellNode.HasChildNode(ChildIndex));
					}
				}
			}
		});
	}
}

FBox UWorldPartitionEditorSpatialHash::GetActorBounds(const FWorldPartitionHandle& InActorHandle) const
{
	FBox ActorBounds;
	if (InActorHandle->GetIsSpatiallyLoaded())
	{
		ActorBounds = InActorHandle->GetBounds();
	}

	check(ActorBounds.IsValid);
	return ActorBounds;
}

bool UWorldPartitionEditorSpatialHash::IsActorAlwaysLoaded(const FWorldPartitionHandle& InActorHandle) const
{
	if (!InActorHandle->GetIsSpatiallyLoaded())
	{
		return true;
	}

	// If an actor covers more that 4 levels in the octree (which means 32K cells), treat it as always loaded
	const FBox ActorBounds = GetActorBounds(InActorHandle);
	const int32 ActorLevel = GetLevelForBox(ActorBounds);
	return (ActorLevel > 4);
}

void UWorldPartitionEditorSpatialHash::HashActor(FWorldPartitionHandle& InActorHandle)
{
	check(InActorHandle.IsValid());

	if (IsActorAlwaysLoaded(InActorHandle))
	{
		AlwaysLoadedCell->AddActor(InActorHandle);
	}
	else
	{
		const FBox ActorBounds = GetActorBounds(InActorHandle);
		const int32 CurrentLevel = GetLevelForBox(Bounds);

		ForEachIntersectingCells(ActorBounds, 0, [&](const FCellCoord& CellCoord)
		{
			UWorldPartitionEditorCell* EditorCell = nullptr;
			if (UWorldPartitionEditorCell** EditorCellPtr = HashCells.Find(CellCoord))
			{
				EditorCell = *EditorCellPtr;
			}
			else
			{
				EditorCell = NewObject<UWorldPartitionEditorCell>(this, *FString::Printf(TEXT("EditorCell_S%d_X%lld_Y%lld_Z%lld"), CellSize, CellCoord.X, CellCoord.Y, CellCoord.Z), RF_Transient);
				EditorCell->Bounds = GetCellBounds(CellCoord);

				Cells.Add(EditorCell);
				HashCells.Add(CellCoord, EditorCell);

				Bounds += EditorCell->Bounds;

				// Update parent cells
				FCellCoord LevelCellCoord = CellCoord;
				while (LevelCellCoord.Level < CurrentLevel)
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
			}

			check(EditorCell);
			EditorCell->AddActor(InActorHandle);
		});

		const int32 NewLevel = GetLevelForBox(Bounds);
		check(NewLevel >= CurrentLevel);

		if (NewLevel > CurrentLevel)
		{
			ForEachIntersectingCells(Bounds, CurrentLevel, [&](const FCellCoord& CellCoord)
			{
				if (CurrentLevel ? HashNodes.Contains(CellCoord) : HashCells.Contains(CellCoord))
				{
					FCellCoord LevelCellCoord = CellCoord;
					while (LevelCellCoord.Level < NewLevel)
					{
						const uint32 ChildIndex = LevelCellCoord.GetChildIndex();

						LevelCellCoord = LevelCellCoord.GetParentCellCoord();

						FCellNode& CellNode = HashNodes.FindOrAdd(LevelCellCoord);

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
			});
		}
	}
}

void UWorldPartitionEditorSpatialHash::UnhashActor(FWorldPartitionHandle& InActorHandle)
{
	check(InActorHandle.IsValid());

	if (IsActorAlwaysLoaded(InActorHandle))
	{
		AlwaysLoadedCell->RemoveActor(InActorHandle);
	}
	else
	{
		const FBox ActorBounds = GetActorBounds(InActorHandle);
		const int32 CurrentLevel = GetLevelForBox(Bounds);

		ForEachIntersectingCells(ActorBounds, 0, [&](const FCellCoord& CellCoord)
		{
			UWorldPartitionEditorCell* EditorCell = HashCells.FindChecked(CellCoord);

			EditorCell->RemoveActor(InActorHandle);

			if (!EditorCell->Actors.Num())
			{
				verify(Cells.Remove(EditorCell));
				verify(HashCells.Remove(CellCoord));

				bool bClearChildMask = true;

				FCellCoord LevelCellCoord = CellCoord;
				while (LevelCellCoord.Level < CurrentLevel)
				{
					const uint32 ChildIndex = LevelCellCoord.GetChildIndex();

					LevelCellCoord = LevelCellCoord.GetParentCellCoord();

					FCellNode& CellNode = HashNodes.FindChecked(LevelCellCoord);

					if (bClearChildMask)
					{
						CellNode.RemoveChildNode(ChildIndex);

						if (CellNode.HasChildNodes())
						{
							bClearChildMask = false;
						}
						else
						{
							HashNodes.Remove(LevelCellCoord);
						}
					}

					if (!bClearChildMask)
					{
						break;
					}
				}

				bBoundsDirty = true;
			}
		});
	}

	// Remove from all cells where it's back referenced
	// This is important because the FWorldPartitionHandle will soon become invalid and Actors that were still 
	// referencing this one will not be able to construct valid FWorldPartitionHandle to the actor we're 
	// removing and it'll become impossible to clean the cells of their dangling actors
	TArray<TTuple<UWorldPartitionEditorCell*, FGuid>> BackRefs;
	BackReferences.MultiFind(InActorHandle->GetGuid(), BackRefs);

	for (auto& BackRef : BackRefs)
	{
		BackRef.Get<0>()->RemoveActor(BackRef.Get<1>(), InActorHandle);
	}

	if (BackRefs.Num() != 0)
	{
		BackReferences.Remove(InActorHandle->GetGuid());
	}
}

int32 UWorldPartitionEditorSpatialHash::ForEachIntersectingActor(const FBox& Box, TFunctionRef<void(FWorldPartitionActorDesc*)> InOperation)
{
	int32 NumIntersecting = 0;

	FWorldPartitionActorDesc::GlobalTag++;

	ForEachIntersectingCell(Box, [&](UWorldPartitionEditorCell* EditorCell)
	{
		for(UWorldPartitionEditorCell::FActorHandle& ActorDesc: EditorCell->Actors)
		{
			if (ActorDesc.IsValid() && ActorDesc->Tag != FWorldPartitionActorDesc::GlobalTag)
			{
				if (Box.Intersect(ActorDesc->GetBounds()))
				{
					InOperation(*ActorDesc);
					NumIntersecting++;
				}

				ActorDesc->Tag = FWorldPartitionActorDesc::GlobalTag;
			}
		}
	});

	for(UWorldPartitionEditorCell::FActorHandle& ActorDesc: AlwaysLoadedCell->Actors)
	{
		if (ActorDesc.IsValid() && ActorDesc->Tag != FWorldPartitionActorDesc::GlobalTag)
		{
			if (Box.Intersect(ActorDesc->GetBounds()))
			{
				InOperation(*ActorDesc);
				NumIntersecting++;
			}

			ActorDesc->Tag = FWorldPartitionActorDesc::GlobalTag;
		}
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
	InOperation(AlwaysLoadedCell);
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


void UWorldPartitionEditorSpatialHash::AddBackReference(const FGuid& Reference, UWorldPartitionEditorCell* Cell, const FGuid& Source)
{
	BackReferences.Add(Reference, MakeTuple(Cell, Source));
}

void UWorldPartitionEditorSpatialHash::RemoveBackReference(const FGuid& Reference, UWorldPartitionEditorCell* Cell, const FGuid& Source)
{
	BackReferences.RemoveSingle(Reference, MakeTuple(Cell, Source));
}
#endif

#undef LOCTEXT_NAMESPACE
