// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldPartitionRuntimeSpatialHash.cpp: UWorldPartitionRuntimeSpatialHash implementation
=============================================================================*/
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"

#include "WorldPartition/WorldPartitionRuntimeSpatialHashCell.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"

#include "WorldPartition/RuntimeSpatialHash/RuntimeSpatialHashGridHelper.h"

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "RenderUtils.h"
#include "Algo/Transform.h"
#include "Algo/Copy.h"
#include "Math/Range.h"
#include "UObject/ObjectSaveContext.h"
#include "Components/LineBatchComponent.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "CookPackageSplitter.h"
#include "Misc/Parse.h"

#include "Algo/Find.h"
#include "Algo/ForEach.h"

#include "Engine/WorldComposition.h"
#include "LevelUtils.h"

#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"

extern UNREALED_API class UEditorEngine* GEditor;
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "WorldPartition"

static int32 GShowRuntimeSpatialHashGridLevel = 0;
static FAutoConsoleVariableRef CVarShowRuntimeSpatialHashGridLevel(
	TEXT("wp.Runtime.ShowRuntimeSpatialHashGridLevel"),
	GShowRuntimeSpatialHashGridLevel,
	TEXT("Used to choose which grid level to display when showing world partition runtime hash."));

static int32 GShowRuntimeSpatialHashGridLevelCount = 1;
static FAutoConsoleVariableRef CVarShowRuntimeSpatialHashGridLevelCount(
	TEXT("wp.Runtime.ShowRuntimeSpatialHashGridLevelCount"),
	GShowRuntimeSpatialHashGridLevelCount,
	TEXT("Used to choose how many grid levels to display when showing world partition runtime hash."));

static float GBlockOnSlowStreamingRatio = 0.25f;
static FAutoConsoleVariableRef CVarBlockOnSlowStreamingRatio(
	TEXT("wp.Runtime.BlockOnSlowStreamingRatio"),
	GBlockOnSlowStreamingRatio,
	TEXT("Ratio of DistanceToCell / LoadingRange to use to determine if World Partition streaming needs to block"));

static float GBlockOnSlowStreamingWarningFactor = 2.f;
static FAutoConsoleVariableRef CVarBlockOnSlowStreamingWarningFactor(
	TEXT("wp.Runtime.BlockOnSlowStreamingWarningFactor"),
	GBlockOnSlowStreamingWarningFactor,
	TEXT("Factor of wp.Runtime.BlockOnSlowStreamingRatio we want to start notifying the user"));

#if !UE_BUILD_SHIPPING
static int32 GFilterRuntimeSpatialHashGridLevel = INDEX_NONE;
static FAutoConsoleVariableRef CVarFilterRuntimeSpatialHashGridLevel(
	TEXT("wp.Runtime.FilterRuntimeSpatialHashGridLevel"),
	GFilterRuntimeSpatialHashGridLevel,
	TEXT("Used to choose filter a single world partition runtime hash grid level."));
#endif

static int32 GForceRuntimeSpatialHashZCulling = -1;
static FAutoConsoleVariableRef CVarForceRuntimeSpatialHashZCulling(
	TEXT("wp.Runtime.ForceRuntimeSpatialHashZCulling"),
	GForceRuntimeSpatialHashZCulling,
	TEXT("Used to force the behavior of the runtime hash cells Z culling. Set to 0 to force off, to 1 to force on and any other value to respect the runtime hash setting."));

static bool GetEffectiveEnableZCulling(bool bEnableZCulling)
{
	switch (GForceRuntimeSpatialHashZCulling)
	{
	case 0: return false;
	case 1: return true;
	}

	return bEnableZCulling;
}

// ------------------------------------------------------------------------------------------------
FSpatialHashStreamingGrid::FSpatialHashStreamingGrid()
	: Origin(ForceInitToZero)
	, CellSize(0)
	, LoadingRange(0.0f)
	, bBlockOnSlowStreaming(false)
	, DebugColor(ForceInitToZero)
	, WorldBounds(ForceInitToZero)
	, bClientOnlyVisible(false)
	, HLODLayer(nullptr)
	, OverrideLoadingRange(-1.f)
	, GridHelper(nullptr)
{
}

FSpatialHashStreamingGrid::~FSpatialHashStreamingGrid()
{
	if (GridHelper)
	{
		delete GridHelper;
	}
}

const FSquare2DGridHelper& FSpatialHashStreamingGrid::GetGridHelper() const
{
	if (!GridHelper)
	{
		GridHelper = new FSquare2DGridHelper(WorldBounds, Origin, CellSize);
	}

	check(GridHelper->Levels.Num() == GridLevels.Num());
	check(GridHelper->Origin == Origin);
	check(GridHelper->CellSize == CellSize);
	check(GridHelper->WorldBounds == WorldBounds);

	return *GridHelper;
}

int64 FSpatialHashStreamingGrid::GetCellSize(int32 Level) const
{
	return GetGridHelper().Levels[Level].CellSize;
}

void FSpatialHashStreamingGrid::GetCells(const FWorldPartitionStreamingQuerySource& QuerySource, TSet<const UWorldPartitionRuntimeCell*>& OutCells, bool bEnableZCulling) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSpatialHashStreamingGrid::GetCells_QuerySource);

	auto ShouldAddCell = [](const UWorldPartitionRuntimeCell* Cell, const FWorldPartitionStreamingQuerySource& QuerySource)
	{
		if (Cell->HasDataLayers())
		{
			if (Cell->GetDataLayers().FindByPredicate([&](const FName& DataLayerName) { return QuerySource.DataLayers.Contains(DataLayerName); }))
			{
				return true;
			}
		}
		else if (!QuerySource.bDataLayersOnly)
		{
			return true;
		}

		return false;
	};

	const FSquare2DGridHelper& Helper = GetGridHelper();

	// Spatial Query
	if (QuerySource.bSpatialQuery)
	{
		QuerySource.ForEachShape(GetLoadingRange(), GridName, HLODLayer, /*bProjectIn2D*/ true, [&](const FSphericalSector& Shape)
		{
			Helper.ForEachIntersectingCells(Shape, [&](const FGridCellCoord& Coords)
			{
				if (const FSpatialHashStreamingGridLayerCell* LayerCell = GetLayerCell(Coords))
				{
					for (const UWorldPartitionRuntimeCell* Cell : LayerCell->GridCells)
					{
						if (!bEnableZCulling || TRange<double>(Cell->GetMinMaxZ().X, Cell->GetMinMaxZ().Y).Overlaps(TRange<double>(Shape.GetCenter().Z - Shape.GetRadius(), Shape.GetCenter().Z + Shape.GetRadius())))
						{
							if (ShouldAddCell(Cell, QuerySource))
							{
								OutCells.Add(Cell);
							}
						}
					}
				}
			});
		});
	}

	// Non Spatial (always included)
	const int32 TopLevel = GridLevels.Num() - 1;
	for (const FSpatialHashStreamingGridLayerCell& LayerCell : GridLevels[TopLevel].LayerCells)
	{
		for (const UWorldPartitionRuntimeCell* Cell : LayerCell.GridCells)
		{
			if (ShouldAddCell(Cell, QuerySource))
			{
				OutCells.Add(Cell);
			}
		}
	}	
}

void FSpatialHashStreamingGrid::GetCells(const TArray<FWorldPartitionStreamingSource>& Sources, const UDataLayerSubsystem* DataLayerSubsystem, UWorldPartitionRuntimeHash::FStreamingSourceCells& OutActivateCells, UWorldPartitionRuntimeHash::FStreamingSourceCells& OutLoadCells, bool bEnableZCulling) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSpatialHashStreamingGrid::GetCells);
	
	typedef TMap<FGridCellCoord, TArray<UWorldPartitionRuntimeCell::FStreamingSourceInfo>> FIntersectingCells;
	FIntersectingCells AllActivatedCells;

	const float GridLoadingRange = GetLoadingRange();
	const FSquare2DGridHelper& Helper = GetGridHelper();
	for (const FWorldPartitionStreamingSource& Source : Sources)
	{
		Source.ForEachShape(GridLoadingRange, GridName, HLODLayer, /*bProjectIn2D*/ true, [&](const FSphericalSector& Shape)
		{
			UWorldPartitionRuntimeCell::FStreamingSourceInfo Info(Source, Shape);

			Helper.ForEachIntersectingCells(Shape, [&](const FGridCellCoord& Coords)
			{
				bool bAddedActivatedCell = false;
#if !UE_BUILD_SHIPPING
				if ((GFilterRuntimeSpatialHashGridLevel == INDEX_NONE) || (GFilterRuntimeSpatialHashGridLevel == Coords.Z))
#endif
				{
					if (const FSpatialHashStreamingGridLayerCell* LayerCell = GetLayerCell(Coords))
					{
						for (const UWorldPartitionRuntimeCell* Cell : LayerCell->GridCells)
						{
							if (!bEnableZCulling || TRange<double>(Cell->GetMinMaxZ().X, Cell->GetMinMaxZ().Y).Overlaps(TRange<double>(Shape.GetCenter().Z - Shape.GetRadius(), Shape.GetCenter().Z + Shape.GetRadius())))
							{
								if (!Cell->HasDataLayers() || (DataLayerSubsystem && DataLayerSubsystem->IsAnyDataLayerInEffectiveRuntimeState(Cell->GetDataLayers(), EDataLayerRuntimeState::Activated)))
								{
									if (Source.TargetState == EStreamingSourceTargetState::Loaded)
									{
										OutLoadCells.AddCell(Cell, Info);
									}
									else
									{
										check(Source.TargetState == EStreamingSourceTargetState::Activated);
										OutActivateCells.AddCell(Cell, Info);
										bAddedActivatedCell = !GRuntimeSpatialHashUseAlignedGridLevels && GRuntimeSpatialHashSnapNonAlignedGridLevelsToLowerLevels;
									}
								}
								else if (DataLayerSubsystem && DataLayerSubsystem->IsAnyDataLayerInEffectiveRuntimeState(Cell->GetDataLayers(), EDataLayerRuntimeState::Loaded))
								{
									OutLoadCells.AddCell(Cell, Info);
								}
							}
						}
					}
				}
				if (bAddedActivatedCell)
				{
					AllActivatedCells.FindOrAdd(Coords).Add(Info);
				}
			});
		});
	}

	GetAlwaysLoadedCells(DataLayerSubsystem, OutActivateCells.GetCells(), OutLoadCells.GetCells());

	if (!GRuntimeSpatialHashUseAlignedGridLevels && GRuntimeSpatialHashSnapNonAlignedGridLevelsToLowerLevels)
	{
		auto FindIntersectingParents = [&Helper, this](const FIntersectingCells& InAllCells, const FIntersectingCells& InTestCells, FIntersectingCells& OutIntersectingCells)
		{
			bool bFound = false;
			const int32 AlwaysLoadedLevel = Helper.Levels.Num() - 1;
			for (const auto& InTestCell : InTestCells)
			{
				const FGridCellCoord& TestCell = InTestCell.Key;
				int32 CurrentLevelIndex = TestCell.Z;
				int32 ParentLevelIndex = CurrentLevelIndex + 1;
				// Only test with Parent Level if it's below the AlwaysLoaded Level
				if (ParentLevelIndex < AlwaysLoadedLevel)
				{
					FBox2D CurrentLevelCellBounds;
					Helper.Levels[CurrentLevelIndex].GetCellBounds(FGridCellCoord2(TestCell.X, TestCell.Y), CurrentLevelCellBounds);
					FBox Box(FVector(CurrentLevelCellBounds.Min, 0), FVector(CurrentLevelCellBounds.Max, 0));

					Helper.ForEachIntersectingCells(Box, [&](const FGridCellCoord& IntersectingCoords)
					{
						check(IntersectingCoords.Z >= ParentLevelIndex);
						if (!InAllCells.Contains(IntersectingCoords))
						{
							if (!OutIntersectingCells.Contains(IntersectingCoords))
							{
								OutIntersectingCells.Add(IntersectingCoords, InTestCell.Value);
								bFound = true;
							}
						}
					}, ParentLevelIndex);
				}
			}
			return bFound;
		};
	
		FIntersectingCells AllParentCells;
		FIntersectingCells TestCells = AllActivatedCells;
		FIntersectingCells IntersectingCells;
		bool bFound = false;
		do
		{
			bFound = FindIntersectingParents(AllActivatedCells, TestCells, IntersectingCells);
			if (bFound)
			{
				AllActivatedCells.Append(IntersectingCells);
				AllParentCells.Append(IntersectingCells);
				TestCells = MoveTemp(IntersectingCells);
				check(IntersectingCells.IsEmpty());
			}
		} while (bFound);

		for (const auto& ParentCell : AllParentCells)
		{
			if (const FSpatialHashStreamingGridLayerCell* LayerCell = GetLayerCell(ParentCell.Key))
			{
				for (const UWorldPartitionRuntimeCell* Cell : LayerCell->GridCells)
				{
					if (!Cell->HasDataLayers() || (DataLayerSubsystem && DataLayerSubsystem->IsAnyDataLayerInEffectiveRuntimeState(Cell->GetDataLayers(), EDataLayerRuntimeState::Activated)))
					{
						for (const auto& Info : ParentCell.Value)
						{
							OutActivateCells.AddCell(Cell, Info);
						}
					}
				}
			}
		}
	}
}

const FSpatialHashStreamingGridLayerCell* FSpatialHashStreamingGrid::GetLayerCell(const FGridCellCoord& Coords) const
{
	check(GridLevels.IsValidIndex(Coords.Z));
	if (const int32* LayerCellIndexPtr = GridLevels[Coords.Z].LayerCellsMapping.Find(Coords.Y * GetGridHelper().Levels[Coords.Z].GridSize + Coords.X))
	{
		return &GridLevels[Coords.Z].LayerCells[*LayerCellIndexPtr];
	}
	return nullptr;
}

void FSpatialHashStreamingGrid::GetAlwaysLoadedCells(const UDataLayerSubsystem* DataLayerSubsystem, TSet<const UWorldPartitionRuntimeCell*>& OutActivateCells, TSet<const UWorldPartitionRuntimeCell*>& OutLoadCells) const
{
	if (GridLevels.Num() > 0)
	{
		const int32 TopLevel = GridLevels.Num() - 1;
		for (const FSpatialHashStreamingGridLayerCell& LayerCell : GridLevels[TopLevel].LayerCells)
		{
			for (const UWorldPartitionRuntimeCell* Cell : LayerCell.GridCells)
			{
				if (!Cell->HasDataLayers() || (DataLayerSubsystem && DataLayerSubsystem->IsAnyDataLayerInEffectiveRuntimeState(Cell->GetDataLayers(), EDataLayerRuntimeState::Activated)))
				{
					check(Cell->IsAlwaysLoaded() || Cell->HasDataLayers());
					OutActivateCells.Add(Cell);
				}
				else if(DataLayerSubsystem && DataLayerSubsystem->IsAnyDataLayerInEffectiveRuntimeState(Cell->GetDataLayers(), EDataLayerRuntimeState::Loaded))
				{
					check(Cell->HasDataLayers());
					OutLoadCells.Add(Cell);
				}
			}
		}
	}
}

void FSpatialHashStreamingGrid::GetFilteredCellsForDebugDraw(const FSpatialHashStreamingGridLayerCell* LayerCell, const UDataLayerSubsystem* DataLayerSubsystem, TArray<const UWorldPartitionRuntimeCell*>& FilteredCells) const
{
	FilteredCells.Reset();
	if (LayerCell)
	{
		Algo::CopyIf(LayerCell->GridCells, FilteredCells, [DataLayerSubsystem](const UWorldPartitionRuntimeCell* GridCell)
		{ 
			if (GridCell->IsDebugShown())
			{
				EStreamingStatus StreamingStatus = GridCell->GetStreamingStatus();
				const TArray<FName>& DataLayers = GridCell->GetDataLayers();
				return (!DataLayers.Num() || 
						DataLayerSubsystem->IsAnyDataLayerInEffectiveRuntimeState(DataLayers, EDataLayerRuntimeState::Loaded) ||
						DataLayerSubsystem->IsAnyDataLayerInEffectiveRuntimeState(DataLayers, EDataLayerRuntimeState::Activated) ||
						((StreamingStatus != LEVEL_Unloaded) && (StreamingStatus != LEVEL_UnloadedButStillAround)));
			}
			return false;
		});
	}
	if (FilteredCells.IsEmpty())
	{
		const UWorldPartitionRuntimeCell* DefaultEmptyCell = UWorldPartitionRuntimeCell::StaticClass()->GetDefaultObject<UWorldPartitionRuntimeCell>();
		FilteredCells.Add(DefaultEmptyCell);
	}
}

EWorldPartitionRuntimeCellVisualizeMode FSpatialHashStreamingGrid::GetStreamingCellVisualizeMode() const
{
	const EWorldPartitionRuntimeCellVisualizeMode VisualizeMode = FWorldPartitionDebugHelper::IsRuntimeSpatialHashCellStreamingPriorityShown() ? EWorldPartitionRuntimeCellVisualizeMode::StreamingPriority : EWorldPartitionRuntimeCellVisualizeMode::StreamingStatus;
	return VisualizeMode;
}

void FSpatialHashStreamingGrid::Draw3D(UWorld* World, const TArray<FWorldPartitionStreamingSource>& Sources, const FTransform& Transform) const
{
	const EWorldPartitionRuntimeCellVisualizeMode VisualizeMode = GetStreamingCellVisualizeMode();
	const UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
	TMap<FName, FColor> DataLayerDebugColors;
	if (DataLayerSubsystem)
	{
		DataLayerSubsystem->GetDataLayerDebugColors(DataLayerDebugColors);
	}

	const FSquare2DGridHelper& Helper = GetGridHelper();
	int32 MinGridLevel = FMath::Clamp<int32>(GShowRuntimeSpatialHashGridLevel, 0, GridLevels.Num() - 1);
	int32 MaxGridLevel = FMath::Clamp<int32>(MinGridLevel + GShowRuntimeSpatialHashGridLevelCount - 1, 0, GridLevels.Num() - 1);
	const float GridViewMinimumSizeInCellCount = 5.f;
	const float GridViewLoadingRangeExtentRatio = 1.5f;
	const float GridLoadingRange = GetLoadingRange();
	const FVector MinExtent(CellSize * GridViewMinimumSizeInCellCount);
	TArray<const UWorldPartitionRuntimeCell*> FilteredCells;
	TSet<const UWorldPartitionRuntimeCell*> DrawnCells;

	for (const FWorldPartitionStreamingSource& Source : Sources)
	{
		FVector StartTrace = Source.Location + FVector(0.f, 0.f, 100.f);
		FVector EndTrace = StartTrace - FVector(0.f, 0.f, 1000000.f);
		double Z = Source.Location.Z;
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, StartTrace, EndTrace, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(DebugWorldPartitionTrace), true)))
		{
			Z = Hit.ImpactPoint.Z;
		}

		FBox Region = Source.CalcBounds(GridLoadingRange, GridName, HLODLayer);
		Region += FBox(Region.GetCenter() - MinExtent, Region.GetCenter() + MinExtent);

		for (int32 GridLevel = MinGridLevel; GridLevel <= MaxGridLevel; ++GridLevel)
		{
			Helper.Levels[GridLevel].ForEachIntersectingCells(Region, [&](const FGridCellCoord2& Coords)
			{
				const int32* LayerCellIndexPtr = GridLevels[GridLevel].LayerCellsMapping.Find(Coords.Y * Helper.Levels[GridLevel].GridSize + Coords.X);
				const FSpatialHashStreamingGridLayerCell* LayerCell = LayerCellIndexPtr ? &GridLevels[GridLevel].LayerCells[*LayerCellIndexPtr] : nullptr;
				GetFilteredCellsForDebugDraw(LayerCell, DataLayerSubsystem, FilteredCells);
				check(FilteredCells.Num());

				FBox2D CellWorldBounds;
				Helper.Levels[GridLevel].GetCellBounds(FGridCellCoord2(Coords.X, Coords.Y), CellWorldBounds);
				double CellSizeY = CellWorldBounds.GetSize().Y / FilteredCells.Num();
				CellWorldBounds.Max.Y = CellWorldBounds.Min.Y + CellSizeY;
				FVector BoundsExtent(CellWorldBounds.GetExtent(), 100.f);
				FVector BoundsOrigin(CellWorldBounds.GetCenter(), Z);
				FBox CellBox = FBox::BuildAABB(BoundsOrigin, BoundsExtent);
				FTranslationMatrix CellOffsetMatrix(FVector(0.f, CellSizeY, 0.f));

				for (const UWorldPartitionRuntimeCell* Cell : FilteredCells)
				{
					bool bIsAlreadyInSet = false;
					DrawnCells.Add(Cell, &bIsAlreadyInSet);
					if (bIsAlreadyInSet)
					{
						continue;
					}

					// Draw Cell using its debug color
					FColor CellColor = Cell->GetDebugColor(VisualizeMode).ToFColor(false).WithAlpha(64);
					DrawDebugSolidBox(World, CellBox, CellColor, Transform, false, -1.f, 255);
					FVector CellPos = Transform.TransformPosition(CellBox.GetCenter());
					DrawDebugBox(World, CellPos, BoundsExtent, Transform.GetRotation(), CellColor.WithAlpha(255), false, -1.f, 255, 10.f);

					// Draw Cell's DataLayers colored boxes
					if (DataLayerDebugColors.Num() && Cell->GetDataLayers().Num() > 0)
					{
						FBox DataLayerColoredBox = CellBox;
						double DataLayerSizeX = DataLayerColoredBox.GetSize().X / (5 * Cell->GetDataLayers().Num()); // Use 20% of cell's width
						DataLayerColoredBox.Max.X = DataLayerColoredBox.Min.X + DataLayerSizeX; 
						FTranslationMatrix DataLayerOffsetMatrix(FVector(DataLayerSizeX, 0, 0));
						for (const FName& DataLayer : Cell->GetDataLayers())
						{
							const FColor& DataLayerColor = DataLayerDebugColors[DataLayer];
							DrawDebugSolidBox(World, DataLayerColoredBox, DataLayerColor, Transform, false, -1.f, 255);
							DataLayerColoredBox = DataLayerColoredBox.TransformBy(DataLayerOffsetMatrix);
						}
					}
					CellBox = CellBox.TransformBy(CellOffsetMatrix);
				}
			});
		}

		// Draw Streaming Source
		const FColor Color = Source.GetDebugColor();
		Source.ForEachShape(GetLoadingRange(), GridName, HLODLayer, /*bProjectIn2D*/ true, [&Color, &Z, &Transform, &World, this](const FSphericalSector& Shape)
		{
			FSphericalSector ZOffsettedShape = Shape;
			ZOffsettedShape.SetCenter(FVector(FVector2D(ZOffsettedShape.GetCenter()), Z));
			DrawStreamingSource3D(World, ZOffsettedShape, Transform, Color);
		});
	}
}

void FSpatialHashStreamingGrid::DrawStreamingSource3D(UWorld* World, const FSphericalSector& InShape, const FTransform& InTransform, const FColor& InColor) const
{
	if (InShape.IsSphere())
	{
		FVector Location = InTransform.TransformPosition(InShape.GetCenter());
		DrawDebugSphere(World, Location, InShape.GetRadius(), 32, InColor, false, -1.f, 0, 20.f);
	}
	else
	{
		ULineBatchComponent* const LineBatcher = World->LineBatcher;
		if (LineBatcher)
		{
			FSphericalSector Shape = InShape;
			Shape.SetAxis(InTransform.TransformVector(Shape.GetAxis()));
			Shape.SetCenter(InTransform.TransformPosition(Shape.GetCenter()));

			TArray<TPair<FVector, FVector>> Lines = Shape.BuildDebugMesh();
			TArray<FBatchedLine> BatchedLines;
			BatchedLines.Empty(Lines.Num());
			for (const auto& Line : Lines)
			{
				BatchedLines.Emplace(FBatchedLine(Line.Key, Line.Value, InColor, LineBatcher->DefaultLifeTime, 20.f, SDPG_World));
			};
			LineBatcher->DrawLines(BatchedLines);
		}
	}
}

void FSpatialHashStreamingGrid::Draw2D(UCanvas* Canvas, UWorld* World, const TArray<FWorldPartitionStreamingSource>& Sources, const FBox& Region, const FBox2D& GridScreenBounds, TFunctionRef<FVector2D(const FVector2D&)> WorldToScreen) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSpatialHashStreamingGrid::Draw2D);

	const EWorldPartitionRuntimeCellVisualizeMode VisualizeMode = GetStreamingCellVisualizeMode();
	const UDataLayerSubsystem* DataLayerSubsystem = World->GetSubsystem<UDataLayerSubsystem>();
	TMap<FName, FColor> DataLayerDebugColors;
	if (DataLayerSubsystem)
	{
		DataLayerSubsystem->GetDataLayerDebugColors(DataLayerDebugColors);
	}

	FCanvas* CanvasObject = Canvas->Canvas;
	int32 MinGridLevel = FMath::Clamp<int32>(GShowRuntimeSpatialHashGridLevel, 0, GridLevels.Num() - 1);
	int32 MaxGridLevel = FMath::Clamp<int32>(MinGridLevel + GShowRuntimeSpatialHashGridLevelCount - 1, 0, GridLevels.Num() - 1);

	// Precompute a cell coordinate text with/height using a generic coordinate
	// This will be used later to filter out drawing of cell coordinates (avoid doing expesive calls to Canvas->StrLen)
	const FString CellCoordString = UWorldPartitionRuntimeSpatialHash::GetCellCoordString(FGridCellCoord(88,88,88));
	float MaxCellCoordTextWidth, MaxCellCoordTextHeight;
	Canvas->StrLen(GEngine->GetTinyFont(), CellCoordString, MaxCellCoordTextWidth, MaxCellCoordTextHeight);

	TArray<const UWorldPartitionRuntimeCell*> FilteredCells;
	for (int32 GridLevel = MinGridLevel; GridLevel <= MaxGridLevel; ++GridLevel)
	{
		// Draw Grid cells at desired grid level
		const FSquare2DGridHelper& Helper = GetGridHelper();
		Helper.Levels[GridLevel].ForEachIntersectingCells(Region, [&](const FGridCellCoord2& Coords)
		{
			FBox2D CellWorldBounds;
			Helper.Levels[GridLevel].GetCellBounds(FGridCellCoord2(Coords.X, Coords.Y), CellWorldBounds);
			FBox2D CellScreenBounds = FBox2D(WorldToScreen(CellWorldBounds.Min), WorldToScreen(CellWorldBounds.Max));
			// Clamp inside grid bounds
			if (!GridScreenBounds.IsInside(CellScreenBounds))
			{
				CellScreenBounds.Min.X = FMath::Clamp(CellScreenBounds.Min.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				CellScreenBounds.Min.Y = FMath::Clamp(CellScreenBounds.Min.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				CellScreenBounds.Max.X = FMath::Clamp(CellScreenBounds.Max.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				CellScreenBounds.Max.Y = FMath::Clamp(CellScreenBounds.Max.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
			}
			else
			{
				FGridCellCoord CellGlobalCoords;
				verify(Helper.GetCellGlobalCoords(FGridCellCoord(Coords.X, Coords.Y, GridLevel), CellGlobalCoords));
				const FString CellCoordString = UWorldPartitionRuntimeSpatialHash::GetCellCoordString(CellGlobalCoords);
				const FVector2D CellBoundsSize = CellScreenBounds.GetSize();
				if (MaxCellCoordTextWidth < CellBoundsSize.X && MaxCellCoordTextHeight < CellBoundsSize.Y)
				{
					float CellCoordTextWidth, CellCoordTextHeight;
					Canvas->StrLen(GEngine->GetTinyFont(), CellCoordString, CellCoordTextWidth, CellCoordTextHeight);
					FVector2D GridInfoPos = CellScreenBounds.GetCenter() - FVector2D(CellCoordTextWidth / 2, CellCoordTextHeight / 2);
					Canvas->SetDrawColor(255, 255, 0);
					Canvas->DrawText(GEngine->GetTinyFont(), CellCoordString, GridInfoPos.X, GridInfoPos.Y);
				}
			}

			const int32* LayerCellIndexPtr = GridLevels[GridLevel].LayerCellsMapping.Find(Coords.Y * Helper.Levels[GridLevel].GridSize + Coords.X);
			const FSpatialHashStreamingGridLayerCell* LayerCell = LayerCellIndexPtr ? &GridLevels[GridLevel].LayerCells[*LayerCellIndexPtr] : nullptr;
			GetFilteredCellsForDebugDraw(LayerCell, DataLayerSubsystem, FilteredCells);
			check(FilteredCells.Num());

			FVector2D CellBoundsSize = CellScreenBounds.GetSize();
			CellBoundsSize.Y /= FilteredCells.Num();
			FVector2D CellOffset(0.f, 0.f);
			for (const UWorldPartitionRuntimeCell* Cell : FilteredCells)
			{
				// Draw Cell using its debug color
				FVector2D StartPos = CellScreenBounds.Min + CellOffset;
				FCanvasTileItem Item(StartPos, GWhiteTexture, CellBoundsSize, Cell->GetDebugColor(VisualizeMode));
				Item.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Item);
				CellOffset.Y += CellBoundsSize.Y;

				// Draw Cell's DataLayers colored boxes
				if (DataLayerDebugColors.Num() && Cell->GetDataLayers().Num() > 0)
				{
					FVector2D DataLayerOffset(0, 0);
					FVector2D DataLayerColoredBoxSize = CellBoundsSize;
					DataLayerColoredBoxSize.X /= (5 * Cell->GetDataLayers().Num()); // Use 20% of cell's width
					for (const FName& DataLayer : Cell->GetDataLayers())
					{
						const FColor& DataLayerColor = DataLayerDebugColors[DataLayer];
						FCanvasTileItem DataLayerItem(StartPos + DataLayerOffset, GWhiteTexture, DataLayerColoredBoxSize, DataLayerColor);
						Canvas->DrawItem(DataLayerItem);
						DataLayerOffset.X += DataLayerColoredBoxSize.X;
					}
				}
			}

			// Draw cell bounds
			FCanvasBoxItem Box(CellScreenBounds.Min, CellScreenBounds.GetSize());
			Box.SetColor(FLinearColor::Black);
			Box.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Box);
		});

		// Draw X/Y Axis
		{
			FCanvasLineItem Axis;
			Axis.LineThickness = 3;
			{
				Axis.SetColor(FLinearColor::Red);
				FVector2D LineStart = WorldToScreen(FVector2D(-1638400.f, 0.f));
				FVector2D LineEnd = WorldToScreen(FVector2D(1638400.f, 0.f));
				LineStart.X = FMath::Clamp(LineStart.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				LineStart.Y = FMath::Clamp(LineStart.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				LineEnd.X = FMath::Clamp(LineEnd.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				LineEnd.Y = FMath::Clamp(LineEnd.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				Axis.Draw(CanvasObject, LineStart, LineEnd);
			}
			{
				Axis.SetColor(FLinearColor::Green);
				FVector2D LineStart = WorldToScreen(FVector2D(0.f, -1638400.f));
				FVector2D LineEnd = WorldToScreen(FVector2D(0.f, 1638400.f));
				LineStart.X = FMath::Clamp(LineStart.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				LineStart.Y = FMath::Clamp(LineStart.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				LineEnd.X = FMath::Clamp(LineEnd.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				LineEnd.Y = FMath::Clamp(LineEnd.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				Axis.Draw(CanvasObject, LineStart, LineEnd);
			}
		}

		// Draw Streaming Sources
		const float GridLoadingRange = GetLoadingRange();
		for (const FWorldPartitionStreamingSource& Source : Sources)
		{
			const FColor Color = Source.GetDebugColor();
			Source.ForEachShape(GridLoadingRange, GridName, HLODLayer, /*bProjectIn2D*/ true, [&Color, &Canvas, &WorldToScreen, this](const FSphericalSector& Shape)
			{
				DrawStreamingSource2D(Canvas, Shape, WorldToScreen, Color);
			});
		}

		FCanvasBoxItem Box(GridScreenBounds.Min, GridScreenBounds.GetSize());
		Box.SetColor(DebugColor);
		Canvas->DrawItem(Box);
	}

	// Draw WorldBounds
	FBox2D WorldScreenBounds = FBox2D(WorldToScreen(FVector2D(WorldBounds.Min)), WorldToScreen(FVector2D(WorldBounds.Max)));
	if (!GridScreenBounds.IsInside(WorldScreenBounds))
	{
		WorldScreenBounds.Min.X = FMath::Clamp(WorldScreenBounds.Min.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
		WorldScreenBounds.Min.Y = FMath::Clamp(WorldScreenBounds.Min.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
		WorldScreenBounds.Max.X = FMath::Clamp(WorldScreenBounds.Max.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
		WorldScreenBounds.Max.Y = FMath::Clamp(WorldScreenBounds.Max.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
	}
	FCanvasBoxItem Box(WorldScreenBounds.Min, WorldScreenBounds.GetSize());
	Box.SetColor(FColor::Yellow);
	Box.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Box);
}

void FSpatialHashStreamingGrid::DrawStreamingSource2D(UCanvas* Canvas, const FSphericalSector& Shape, TFunctionRef<FVector2D(const FVector2D&)> WorldToScreen, const FColor& Color) const
{
	check(!Shape.IsNearlyZero())

	FCanvasLineItem LineItem;
	LineItem.LineThickness = 2;
	LineItem.SetColor(Color);

	// Spherical Sector
	const FVector2D Center2D(FVector2D(Shape.GetCenter()));
	const FSphericalSector::FReal Angle = Shape.GetAngle();
	const int32 MaxSegments = FMath::Max(4, FMath::CeilToInt(64 * Angle / 360.f));
	const float AngleIncrement = Angle / MaxSegments;
	const FVector2D Axis = FVector2D(Shape.GetAxis());
	const FVector Startup = FRotator(0, -0.5f * Angle, 0).RotateVector(Shape.GetScaledAxis());
	FCanvas* CanvasObject = Canvas->Canvas;

	FVector2D LineStart = FVector2D(Startup);
	if (!Shape.IsSphere())
	{
		// Draw sector start axis
		LineItem.Draw(CanvasObject, WorldToScreen(Center2D), WorldToScreen(Center2D + LineStart));
	}
	// Draw sector Arc
	for (int32 i = 1; i <= MaxSegments; i++)
	{
		FVector2D LineEnd = FVector2D(FRotator(0, AngleIncrement * i, 0).RotateVector(Startup));
		LineItem.Draw(CanvasObject, WorldToScreen(Center2D + LineStart), WorldToScreen(Center2D + LineEnd));
		LineStart = LineEnd;
	}
	// If sphere, close circle, else draw sector end axis
	LineItem.Draw(CanvasObject, WorldToScreen(Center2D + LineStart), WorldToScreen(Center2D + (Shape.IsSphere() ? FVector2D(Startup) : FVector2D::ZeroVector)));

	// Draw direction vector
	LineItem.Draw(CanvasObject, WorldToScreen(Center2D), WorldToScreen(Center2D + Axis * Shape.GetRadius()));
}

// ------------------------------------------------------------------------------------------------

ASpatialHashRuntimeGridInfo::ASpatialHashRuntimeGridInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bListedInSceneOutliner = false;
#endif
}

UWorldPartitionRuntimeSpatialHash::UWorldPartitionRuntimeSpatialHash(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, bPreviewGrids(false)
#endif
{}

void UWorldPartitionRuntimeSpatialHash::PreSave(const class ITargetPlatform* TargetPlatform)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	Super::PreSave(TargetPlatform);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
}

void UWorldPartitionRuntimeSpatialHash::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

	if (!IsRunningCookCommandlet())
	{
		// We don't want this to be persisted but we can't set the property Transient as it is NonPIEDuplicateTransient and those flags aren't compatible
		// If at some point GenerateStreaming is done after duplication we can remove this code.
		StreamingGrids.Empty();
	}
}

FString UWorldPartitionRuntimeSpatialHash::GetCellCoordString(const FGridCellCoord& InCellGlobalCoord)
{
	return FString::Printf(TEXT("L%d_X%d_Y%d"), InCellGlobalCoord.Z, InCellGlobalCoord.X, InCellGlobalCoord.Y);
}

#if WITH_EDITOR
void UWorldPartitionRuntimeSpatialHash::DrawPreview() const
{
	GridPreviewer.Draw(GetWorld(), Grids, bPreviewGrids);
}

void UWorldPartitionRuntimeSpatialHash::SetDefaultValues()
{
	check(!Grids.Num());

	FSpatialHashRuntimeGrid& MainGrid = Grids.AddDefaulted_GetRef();
	MainGrid.GridName = TEXT("MainGrid");
	MainGrid.CellSize = 12800;
	MainGrid.LoadingRange = 25600;
	MainGrid.DebugColor = FLinearColor::Gray;
}

bool UWorldPartitionRuntimeSpatialHash::GenerateStreaming(UWorldPartitionStreamingPolicy* StreamingPolicy, const FActorClusterContext& ActorClusterContext, TArray<FString>* OutPackagesToGenerate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::GenerateStreaming);
	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	UE_SCOPED_TIMER(TEXT("GenerateStreaming"), LogWorldPartition, Display);
	
	if (!Grids.Num())
	{
		UE_LOG(LogWorldPartition, Error, TEXT("Invalid partition grids setup"));
		return false;
	}

	// Fix case where StreamingGrids might have been persisted.
	StreamingGrids.Empty();

	// Append grids from ASpatialHashRuntimeGridInfo actors to runtime spatial hash grids
	TArray<FSpatialHashRuntimeGrid> AllGrids;
	AllGrids.Append(Grids);

	const FActorContainerInstance* ContainerInstance = ActorClusterContext.GetClusterInstance(WorldPartition);
	check(ContainerInstance);

	for (auto& ActorDescViewPair : ContainerInstance->ActorDescViewMap)
	{
		const FWorldPartitionActorDescView& ActorDescView = *ActorDescViewPair.Value;
		if (ActorDescView.GetActorNativeClass()->IsChildOf<ASpatialHashRuntimeGridInfo>())
		{
			FWorldPartitionReference Ref(WorldPartition, ActorDescView.GetGuid());
			if (ASpatialHashRuntimeGridInfo* RuntimeGridActor = Cast<ASpatialHashRuntimeGridInfo>(Ref->GetActor()))
			{
				AllGrids.Add(RuntimeGridActor->GridSettings);
			}
		}
	}

	TMap<FName, int32> GridsMapping;
	GridsMapping.Add(NAME_None, 0);
	for (int32 i = 0; i < AllGrids.Num(); i++)
	{
		const FSpatialHashRuntimeGrid& Grid = AllGrids[i];
		check(!GridsMapping.Contains(Grid.GridName));
		GridsMapping.Add(Grid.GridName, i);
	}

	TArray<TArray<const FActorClusterInstance*>> GridActors;
	GridActors.InsertDefaulted(0, AllGrids.Num());

	for (const FActorClusterInstance& ClusterInstance : ActorClusterContext.GetClusterInstances())
	{
		check(ClusterInstance.Cluster);
		int32* FoundIndex = GridsMapping.Find(ClusterInstance.Cluster->RuntimeGrid);
		if (!FoundIndex)
		{
			UE_LOG(LogWorldPartition, Error, TEXT("Invalid partition grid '%s' referenced by actor cluster"), *ClusterInstance.Cluster->RuntimeGrid.ToString());
		}

		int32 GridIndex = FoundIndex ? *FoundIndex : 0;
		GridActors[GridIndex].Add(&ClusterInstance);
	}
	
	const FBox WorldBounds = ActorClusterContext.GetClusterInstance(WorldPartition)->Bounds;
	for (int32 GridIndex=0; GridIndex < AllGrids.Num(); GridIndex++)
	{
		const FSpatialHashRuntimeGrid& Grid = AllGrids[GridIndex];
		const FSquare2DGridHelper PartionedActors = GetPartitionedActors(WorldPartition, WorldBounds, Grid, GridActors[GridIndex]);
		if (!CreateStreamingGrid(Grid, PartionedActors, StreamingPolicy, OutPackagesToGenerate))
		{
			return false;
		}
	}

	return true;
}

void UWorldPartitionRuntimeSpatialHash::DumpStateLog(FHierarchicalLogArchive& Ar)
{
	Super::DumpStateLog(Ar);

	for (FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
	{
		Ar.Printf(TEXT("----------------------------------------------------------------------------------------------------------------"));
		Ar.Printf(TEXT("%s - Runtime Spatial Hash - Streaming Grid - %s"), *GetWorld()->GetName(), *StreamingGrid.GridName.ToString());
		Ar.Printf(TEXT("----------------------------------------------------------------------------------------------------------------"));
		Ar.Printf(TEXT("            Origin: %s"), *StreamingGrid.Origin.ToString());
		Ar.Printf(TEXT("         Cell Size: %d"), StreamingGrid.CellSize);
		Ar.Printf(TEXT("      World Bounds: %s"), *StreamingGrid.WorldBounds.ToString());
		Ar.Printf(TEXT("     Loading Range: %3.2f"), StreamingGrid.LoadingRange);
		Ar.Printf(TEXT("Block Slow Loading: %s"), StreamingGrid.bBlockOnSlowStreaming ? TEXT("Yes") : TEXT("No"));
		Ar.Printf(TEXT(" ClientOnlyVisible: %s"), StreamingGrid.bClientOnlyVisible ? TEXT("Yes") : TEXT("No"));
		Ar.Printf(TEXT(""));
		if (const UHLODLayer* HLODLayer = StreamingGrid.HLODLayer)
		{
			Ar.Printf(TEXT("    HLOD Layer: %s"), *HLODLayer->GetName());
		}

		struct FGridLevelStats
		{
			int32 CellCount;
			int64 CellSize;
			int32 ActorCount;
		};

		TArray<FGridLevelStats> LevelsStats;
		int32 TotalActorCount = 0;
			
		{
			int32 Level = 0;
			for (FSpatialHashStreamingGridLevel& GridLevel : StreamingGrid.GridLevels)
			{
				int32 LevelCellCount = 0;
				int32 LevelActorCount = 0;
				for (FSpatialHashStreamingGridLayerCell& LayerCell : GridLevel.LayerCells)
				{
					LevelCellCount += LayerCell.GridCells.Num();
					for (TObjectPtr<UWorldPartitionRuntimeSpatialHashCell>& Cell : LayerCell.GridCells)
					{
						LevelActorCount += Cell->GetActorCount();
					}
				}
				LevelsStats.Add({ LevelCellCount, ((int64)StreamingGrid.CellSize << (int64)Level), LevelActorCount });
				TotalActorCount += LevelActorCount;
				++Level;
			}
			TotalActorCount = (TotalActorCount > 0) ? TotalActorCount : 1;
		}

		{
			FHierarchicalLogArchive::FIndentScope IndentScope = Ar.PrintfIndent(TEXT("Grid Levels: %d"), StreamingGrid.GridLevels.Num());
			for (int Level=0; Level<LevelsStats.Num(); ++Level)
			{
				Ar.Printf(TEXT("Level %2d: Cell Count %4d | Cell Size %7lld | Actor Count %4d (%3.1f%%)"), Level, LevelsStats[Level].CellCount, LevelsStats[Level].CellSize, LevelsStats[Level].ActorCount, (100.f*LevelsStats[Level].ActorCount)/TotalActorCount);
			}
		}

		{
			Ar.Printf(TEXT(""));
			int32 Level = 0;
			for (const FSpatialHashStreamingGridLevel& GridLevel : StreamingGrid.GridLevels)
			{
				FHierarchicalLogArchive::FIndentScope LevelIndentScope = Ar.PrintfIndent(TEXT("Content of Grid Level %d"), Level++);

				for (const FSpatialHashStreamingGridLayerCell& LayerCell : GridLevel.LayerCells)
				{
					for (const TObjectPtr<UWorldPartitionRuntimeSpatialHashCell>& Cell : LayerCell.GridCells)
					{
						FHierarchicalLogArchive::FIndentScope CellIndentScope = Ar.PrintfIndent(TEXT("Content of Cell %s"), *Cell->GetDebugName());
						Cell->DumpStateLog(Ar);
					}
				}
			}
		}
		Ar.Printf(TEXT(""));
	}	
}

FName UWorldPartitionRuntimeSpatialHash::GetCellName(UWorldPartition* WorldPartition, FName InGridName, const FGridCellCoord& InCellGlobalCoord, const FDataLayersID& InDataLayerID)
{
	const FString PackageName = FPackageName::GetShortName(WorldPartition->GetPackage());
	const FString PackageNameNoPIEPrefix = UWorld::RemovePIEPrefix(PackageName);

	return FName(*FString::Printf(TEXT("%s_%s_%s_DL%X"), *PackageNameNoPIEPrefix, *InGridName.ToString(), *GetCellCoordString(InCellGlobalCoord), InDataLayerID.GetHash()));
}

bool UWorldPartitionRuntimeSpatialHash::GetPreviewGrids() const
{
	return bPreviewGrids;
}

void UWorldPartitionRuntimeSpatialHash::SetPreviewGrids(bool bInPreviewGrids)
{
	Modify(false);
	bPreviewGrids = bInPreviewGrids;
}

FName UWorldPartitionRuntimeSpatialHash::GetCellName(FName InGridName, const FGridCellCoord& InCellGlobalCoord, const FDataLayersID& InDataLayerID) const
{
	UWorldPartition* WorldPartition = GetOuterUWorldPartition();
	return UWorldPartitionRuntimeSpatialHash::GetCellName(WorldPartition, InGridName, InCellGlobalCoord, InDataLayerID);
}

bool UWorldPartitionRuntimeSpatialHash::CreateStreamingGrid(const FSpatialHashRuntimeGrid& RuntimeGrid, const FSquare2DGridHelper& PartionedActors, UWorldPartitionStreamingPolicy* StreamingPolicy, TArray<FString>* OutPackagesToGenerate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateStreamingGrid);

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();
	UWorld* World = WorldPartition->GetWorld();
	const bool bIsMainWorldPartition = (World == WorldPartition->GetTypedOuter<UWorld>());

	FSpatialHashStreamingGrid& CurrentStreamingGrid = StreamingGrids.AddDefaulted_GetRef();
	CurrentStreamingGrid.GridName = RuntimeGrid.GridName;
	CurrentStreamingGrid.Origin = PartionedActors.Origin;
	CurrentStreamingGrid.CellSize = PartionedActors.CellSize;
	CurrentStreamingGrid.WorldBounds = PartionedActors.WorldBounds;
	CurrentStreamingGrid.LoadingRange = RuntimeGrid.LoadingRange;
	CurrentStreamingGrid.bBlockOnSlowStreaming = RuntimeGrid.bBlockOnSlowStreaming;
	CurrentStreamingGrid.DebugColor = RuntimeGrid.DebugColor;
	CurrentStreamingGrid.bClientOnlyVisible = RuntimeGrid.bClientOnlyVisible;
	CurrentStreamingGrid.HLODLayer = RuntimeGrid.HLODLayer;

	// Move actors into the final streaming grids
	CurrentStreamingGrid.GridLevels.Reserve(PartionedActors.Levels.Num());

	TArray<FActorInstance> FilteredActors;
	int32 Level = INDEX_NONE;
	for (const FSquare2DGridHelper::FGridLevel& TempLevel : PartionedActors.Levels)
	{
		Level++;

		FSpatialHashStreamingGridLevel& GridLevel = CurrentStreamingGrid.GridLevels.AddDefaulted_GetRef();

		for (const auto& TempCellMapping : TempLevel.CellsMapping)
		{
			const int64 CellIndex = TempCellMapping.Key;
			const int64 CellCoordX = CellIndex % TempLevel.GridSize;
			const int64 CellCoordY = CellIndex / TempLevel.GridSize;

			const FSquare2DGridHelper::FGridLevel::FGridCell& TempCell = TempLevel.Cells[TempCellMapping.Value];

			for (const FSquare2DGridHelper::FGridLevel::FGridCellDataChunk& GridCellDataChunk : TempCell.GetDataChunks())
			{
				// Cell cannot be treated as always loaded if it has data layers
				const bool bIsCellAlwaysLoaded = (&TempCell == &PartionedActors.GetAlwaysLoadedCell()) && !GridCellDataChunk.HasDataLayers();
				
				FilteredActors.SetNum(0, false);
				FilteredActors.Reset(GridCellDataChunk.GetActors().Num());
				if (GridCellDataChunk.GetActors().Num())
				{
					FWorldPartitionLoadingContext::FDeferred LoadingContext;
					for (const FActorInstance& ActorInstance : GridCellDataChunk.GetActors())
					{
						if (bIsMainWorldPartition && !IsRunningCookCommandlet())
						{
							const FWorldPartitionActorDescView& ActorDescView = ActorInstance.GetActorDescView();

							// In PIE, Always loaded cell is not generated. Instead, always loaded actors will be added to AlwaysLoadedActorsForPIE.
							// This will trigger loading/registration of these actors in the PersistentLevel (if not already loaded).
							// Then, duplication of world for PIE will duplicate only these actors. 
							// When stopping PIE, WorldPartition will release these FWorldPartitionReferences which 
							// will unload actors that were not already loaded in the non PIE world.
							if (bIsCellAlwaysLoaded)
							{
								if (ActorInstance.ContainerInstance->Container == WorldPartition)
								{
									// This will load the actor if it isn't already loaded
									FWorldPartitionReference Reference(WorldPartition, ActorInstance.Actor);									
									if (AActor* AlwaysLoadedActor = FindObject<AActor>(nullptr, *ActorDescView.GetActorPath().ToString()))
									{
										AlwaysLoadedActorsForPIE.Emplace(Reference, AlwaysLoadedActor);

										// Handle child actors
										AlwaysLoadedActor->ForEachComponent<UChildActorComponent>(true, [this, &Reference](UChildActorComponent* ChildActorComponent)
										{
											if (AActor* ChildActor = ChildActorComponent->GetChildActor())
											{
												AlwaysLoadedActorsForPIE.Emplace(Reference, ChildActor);
											}
										});
									}
									continue;
								}
							}
						}

						FilteredActors.Add(ActorInstance);
					}
				}

				if (!FilteredActors.Num())
				{
					continue;
				}
				
				FGridCellCoord CellGlobalCoords;
				verify(PartionedActors.GetCellGlobalCoords(FGridCellCoord(CellCoordX, CellCoordY, Level), CellGlobalCoords));
				FName CellName = GetCellName(CurrentStreamingGrid.GridName, CellGlobalCoords, GridCellDataChunk.GetDataLayersID());

				UWorldPartitionRuntimeSpatialHashCell* StreamingCell = NewObject<UWorldPartitionRuntimeSpatialHashCell>(WorldPartition, StreamingPolicy->GetRuntimeCellClass(), CellName);

				int32 LayerCellIndex;
				int32* LayerCellIndexPtr = GridLevel.LayerCellsMapping.Find(CellIndex);
				if (LayerCellIndexPtr)
				{
					LayerCellIndex = *LayerCellIndexPtr;
				}
				else
				{
					LayerCellIndex = GridLevel.LayerCells.AddDefaulted();
					GridLevel.LayerCellsMapping.Add(CellIndex, LayerCellIndex);
				}

				GridLevel.LayerCells[LayerCellIndex].GridCells.Add(StreamingCell);
				StreamingCell->SetIsAlwaysLoaded(bIsCellAlwaysLoaded);
				StreamingCell->SetDataLayers(GridCellDataChunk.GetDataLayers());
				StreamingCell->Level = Level;
				StreamingCell->SetPriority(RuntimeGrid.Priority);
				FBox2D Bounds;
				verify(TempLevel.GetCellBounds(FGridCellCoord2(CellCoordX, CellCoordY), Bounds));
				StreamingCell->Position = FVector(Bounds.GetCenter(), 0.f);
				const double CellExtent = Bounds.GetExtent().X;
				check(CellExtent < MAX_flt);
				StreamingCell->Extent = CellExtent;
				StreamingCell->SetDebugInfo(CellGlobalCoords.X, CellGlobalCoords.Y, CellGlobalCoords.Z, CurrentStreamingGrid.GridName);
				StreamingCell->SetClientOnlyVisible(CurrentStreamingGrid.bClientOnlyVisible);
				StreamingCell->SetBlockOnSlowLoading(CurrentStreamingGrid.bBlockOnSlowStreaming);
				StreamingCell->SetIsHLOD(RuntimeGrid.HLODLayer ? true : false);

				UE_LOG(LogWorldPartition, Verbose, TEXT("Cell%s %s Actors = %d Bounds (%s)"), bIsCellAlwaysLoaded ? TEXT(" (AlwaysLoaded)") : TEXT(""), *StreamingCell->GetName(), FilteredActors.Num(), *Bounds.ToString());

				check(!StreamingCell->UnsavedActorsContainer);
				for (const FActorInstance& ActorInstance : FilteredActors)
				{
					const FWorldPartitionActorDescView& ActorDescView = ActorInstance.GetActorDescView();
					if (AActor* Actor = FindObject<AActor>(nullptr, *ActorDescView.GetActorPath().ToString()))
					{
						if (ModifiedActorDescListForPIE.GetActorDesc(ActorDescView.GetGuid()) != nullptr)
						{
							// Create an actor container to make sure duplicated actors will share an outer to properly remap inter-actors references
							StreamingCell->UnsavedActorsContainer = NewObject<UActorContainer>(StreamingCell);
							break;
						}
					}
				}

				FVector2D CellMinMaxZ(UE_BIG_NUMBER, -UE_BIG_NUMBER);
				for (const FActorInstance& ActorInstance : FilteredActors)
				{
					const FWorldPartitionActorDescView& ActorDescView = ActorInstance.GetActorDescView();
					StreamingCell->AddActorToCell(ActorDescView, ActorInstance.ContainerInstance->ID, ActorInstance.ContainerInstance->Transform, ActorInstance.ContainerInstance->Container);
					
					CellMinMaxZ.X = FMath::Min(CellMinMaxZ.X, ActorDescView.GetBounds().Min.Z);
					CellMinMaxZ.Y = FMath::Max(CellMinMaxZ.Y, ActorDescView.GetBounds().Max.Z);

					if (ActorInstance.ContainerInstance->ID.IsMainContainer() && StreamingCell->UnsavedActorsContainer)
					{
						if (AActor* Actor = FindObject<AActor>(nullptr, *ActorDescView.GetActorPath().ToString()))
						{
							StreamingCell->UnsavedActorsContainer->Actors.Add(Actor->GetFName(), Actor);

							// Handle child actors
							Actor->ForEachComponent<UChildActorComponent>(true, [StreamingCell](UChildActorComponent* ChildActorComponent)
							{
								if (AActor* ChildActor = ChildActorComponent->GetChildActor())
								{
									StreamingCell->UnsavedActorsContainer->Actors.Add(ChildActor->GetFName(), ChildActor);
								}
							});
						}
					}
					UE_LOG(LogWorldPartition, Verbose, TEXT("  Actor : %s (%s) (Container %s)"), *(ActorDescView.GetActorPath().ToString()), *ActorDescView.GetGuid().ToString(EGuidFormats::UniqueObjectGuid), *ActorInstance.ContainerInstance->ID.ToString());
				}
				StreamingCell->SetMinMaxZ(CellMinMaxZ);

				if (IsRunningCookCommandlet())
				{
					UE_LOG(LogWorldPartition, Log, TEXT("Creating runtime streaming cells %s."), *StreamingCell->GetName());

					if (StreamingCell->GetActorCount())
					{
						// Always loaded cell actors are transfered to World's Persistent Level (see UWorldPartitionRuntimeSpatialHash::PopulateGeneratorPackageForCook)
						if (!StreamingCell->IsAlwaysLoaded())
						{
							if (!OutPackagesToGenerate)
							{
								UE_LOG(LogWorldPartition, Error, TEXT("Error creating runtime streaming cells for cook, OutPackagesToGenerate is null."));
								return false;
							}

							const FString PackageRelativePath = StreamingCell->GetPackageNameToCreate();
							check(!PackageRelativePath.IsEmpty());
							OutPackagesToGenerate->Add(PackageRelativePath);

							// Map relative package to StreamingCell for PopulateGeneratedPackageForCook/PopulateGeneratorPackageForCook
							PackagesToGenerateForCook.Add(PackageRelativePath, StreamingCell);
						}
					}
				}
			}
		}
	}

	return true;
}

bool UWorldPartitionRuntimeSpatialHash::PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath, TArray<UPackage*>& OutModifiedPackages)
{
	OutModifiedPackages.Reset();
	if (UWorldPartitionRuntimeCell** MatchingCell = PackagesToGenerateForCook.Find(InPackageRelativePath))
	{
		UWorldPartitionRuntimeCell* Cell = *MatchingCell;
		if (ensure(Cell))
		{
			return Cell->PopulateGeneratedPackageForCook(InPackage, OutModifiedPackages);
		}
	}
	return false;
}

TArray<UWorldPartitionRuntimeCell*> UWorldPartitionRuntimeSpatialHash::GetAlwaysLoadedCells() const
{
	TArray<UWorldPartitionRuntimeCell*> Result;
	TSet<const UWorldPartitionRuntimeCell*> StreamingCells;
	GetAllStreamingCells(StreamingCells);
	for (const UWorldPartitionRuntimeCell* Cell : StreamingCells)
	{
		if (Cell->IsAlwaysLoaded())
		{
			Result.Add(const_cast<UWorldPartitionRuntimeCell*>(Cell));
		}
	}
	return Result;
}

bool UWorldPartitionRuntimeSpatialHash::PopulateGeneratorPackageForCook(const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& InGeneratedPackages, TArray<UPackage*>& OutModifiedPackages)
{
	check(IsRunningCookCommandlet());

	OutModifiedPackages.Reset();
	for (UWorldPartitionRuntimeCell* Cell : GetAlwaysLoadedCells())
	{
		check(Cell->IsAlwaysLoaded());
		if (!Cell->PopulateGeneratorPackageForCook(OutModifiedPackages))
		{
			return false;
		}
	}

	for (const ICookPackageSplitter::FGeneratedPackageForPreSave& GeneratedPackage : InGeneratedPackages)
	{
		UWorldPartitionRuntimeCell** MatchingCell = PackagesToGenerateForCook.Find(GeneratedPackage.RelativePath);
		UWorldPartitionRuntimeCell* Cell = MatchingCell ? *MatchingCell : nullptr;
		if (!Cell || !Cell->PrepareCellForCook(GeneratedPackage.Package))
		{
			return false;
		}
	}
	return true;
}

void UWorldPartitionRuntimeSpatialHash::FlushStreaming()
{
	StreamingGrids.Empty();
}

#endif //WITH_EDITOR

FAutoConsoleCommand UWorldPartitionRuntimeSpatialHash::OverrideLoadingRangeCommand(
	TEXT("wp.Runtime.OverrideRuntimeSpatialHashLoadingRange"),
	TEXT("Sets runtime loading range. Args -grid=[index] -range=[override_loading_range]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FString ArgString = FString::Join(Args, TEXT(" "));
		int32 GridIndex = 0;
		float OverrideLoadingRange = -1.f;
		FParse::Value(*ArgString, TEXT("grid="), GridIndex);
		FParse::Value(*ArgString, TEXT("range="), OverrideLoadingRange);

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (UWorldPartition* WorldPartition = World->GetWorldPartition())
				{
					if (UWorldPartitionRuntimeSpatialHash* RuntimeSpatialHash = Cast<UWorldPartitionRuntimeSpatialHash>(WorldPartition->RuntimeHash))
					{
						if (RuntimeSpatialHash->StreamingGrids.IsValidIndex(GridIndex))
						{
							RuntimeSpatialHash->StreamingGrids[GridIndex].OverrideLoadingRange = OverrideLoadingRange;
						}
					}
				}
			}
		}
	})
);

bool UWorldPartitionRuntimeSpatialHash::ShouldConsiderClientOnlyVisibleCells() const
{
	const UWorld* World = GetWorld();
	if (World->IsGameWorld())
	{
		ENetMode NetMode = World->GetNetMode();
		if (NetMode == NM_DedicatedServer || NetMode == NM_ListenServer)
		{
			return false;
		}
	}
	return true;
}

// Streaming interface
int32 UWorldPartitionRuntimeSpatialHash::GetAllStreamingCells(TSet<const UWorldPartitionRuntimeCell*>& Cells, bool bAllDataLayers, bool bDataLayersOnly, const TSet<FName>& InDataLayers) const
{
	const bool bShouldConsiderClientOnlyVisible = ShouldConsiderClientOnlyVisibleCells();

	for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
	{
		if (!StreamingGrid.bClientOnlyVisible || bShouldConsiderClientOnlyVisible)
		{
			for (const FSpatialHashStreamingGridLevel& GridLevel : StreamingGrid.GridLevels)
			{
				for (const FSpatialHashStreamingGridLayerCell& LayerCell : GridLevel.LayerCells)
				{
					for (const UWorldPartitionRuntimeSpatialHashCell* Cell : LayerCell.GridCells)
					{
						if (!bDataLayersOnly && !Cell->HasDataLayers())
						{
							Cells.Add(Cell);
						}
						else if (Cell->HasDataLayers() && (bAllDataLayers || Cell->HasAnyDataLayer(InDataLayers)))
						{
							Cells.Add(Cell);
						}
					}
				}
			}
		}
	}

	return Cells.Num();
}

bool UWorldPartitionRuntimeSpatialHash::GetStreamingCells(const FWorldPartitionStreamingQuerySource& QuerySource, TSet<const UWorldPartitionRuntimeCell*>& OutCells) const 
{
	const bool bShouldConsiderClientOnlyVisible = ShouldConsiderClientOnlyVisibleCells();

	for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
	{
		if (!StreamingGrid.bClientOnlyVisible || bShouldConsiderClientOnlyVisible)
		{
			StreamingGrid.GetCells(QuerySource, OutCells, GetEffectiveEnableZCulling(bEnableZCulling));
		}
	}

	return !!OutCells.Num();
}

bool UWorldPartitionRuntimeSpatialHash::GetStreamingCells(const TArray<FWorldPartitionStreamingSource>& Sources, UWorldPartitionRuntimeHash::FStreamingSourceCells& OutActivateCells, UWorldPartitionRuntimeHash::FStreamingSourceCells& OutLoadCells) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::GetStreamingCells);

	const UDataLayerSubsystem* DataLayerSubsystem = GetWorld()->GetSubsystem<UDataLayerSubsystem>();
	const bool bShouldConsiderClientOnlyVisible = ShouldConsiderClientOnlyVisibleCells();

	if (Sources.Num() == 0)
	{
		// Get always loaded cells
		for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
		{
			if (!StreamingGrid.bClientOnlyVisible || bShouldConsiderClientOnlyVisible)
			{
				StreamingGrid.GetAlwaysLoadedCells(DataLayerSubsystem, OutActivateCells.GetCells(), OutLoadCells.GetCells());
			}
		}
	}
	else
	{
		// Get cells based on streaming sources
		for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
		{
			if (!StreamingGrid.bClientOnlyVisible || bShouldConsiderClientOnlyVisible)
			{
				StreamingGrid.GetCells(Sources, DataLayerSubsystem, OutActivateCells, OutLoadCells, GetEffectiveEnableZCulling(bEnableZCulling));
			}
		}
	}

	return !!(OutActivateCells.Num() + OutLoadCells.Num());
}

const TMap<FName, const FSpatialHashStreamingGrid*>& UWorldPartitionRuntimeSpatialHash::GetNameToGridMapping() const
{
	if (NameToGridMapping.IsEmpty())
	{
		for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
		{
			NameToGridMapping.Add(StreamingGrid.GridName, &StreamingGrid);
		}
	}

	return NameToGridMapping;
}

EWorldPartitionStreamingPerformance UWorldPartitionRuntimeSpatialHash::GetStreamingPerformanceForCell(const UWorldPartitionRuntimeCell* Cell) const
{
	// If base class already returning critical. Early out.
	if (Super::GetStreamingPerformanceForCell(Cell) == EWorldPartitionStreamingPerformance::Critical)
	{
		return EWorldPartitionStreamingPerformance::Critical;
	}

	check(Cell->GetBlockOnSlowLoading());
	const double BlockOnSlowStreamingWarningRatio = GBlockOnSlowStreamingRatio * GBlockOnSlowStreamingWarningFactor;
	
	const UWorldPartitionRuntimeSpatialHashCell* StreamingCell = Cast<const UWorldPartitionRuntimeSpatialHashCell>(Cell);
	check(StreamingCell);

	const FSpatialHashStreamingGrid* const* StreamingGrid = GetNameToGridMapping().Find(StreamingCell->GetGridName());
	check(StreamingGrid && *StreamingGrid);
	
	const float LoadingRange = (*StreamingGrid)->LoadingRange;

	if (StreamingCell->CachedIsBlockingSource)
	{
		const double Distance = FMath::Sqrt(StreamingCell->CachedMinSquareDistanceToBlockingSource) - ((double)(*StreamingGrid)->GetCellSize(StreamingCell->Level) / 2);

		const double Ratio = Distance / LoadingRange;

		if (Ratio < GBlockOnSlowStreamingRatio)
		{
			return EWorldPartitionStreamingPerformance::Critical;
		}
		else if (Ratio < BlockOnSlowStreamingWarningRatio)
		{
			return EWorldPartitionStreamingPerformance::Slow;
		}
	}

	return EWorldPartitionStreamingPerformance::Good;
}

FVector2D UWorldPartitionRuntimeSpatialHash::GetDraw2DDesiredFootprint(const FVector2D& CanvasSize) const
{
	return FVector2D(CanvasSize.X * GetFilteredStreamingGrids().Num(), CanvasSize.Y);
}

void UWorldPartitionRuntimeSpatialHash::Draw2D(UCanvas* Canvas, const TArray<FWorldPartitionStreamingSource>& Sources, const FVector2D& PartitionCanvasSize, const FVector2D& Offset) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::Draw2D);

	TArray<const FSpatialHashStreamingGrid*> FilteredStreamingGrids = GetFilteredStreamingGrids();
	if (FilteredStreamingGrids.Num() == 0 || Sources.Num() == 0)
	{
		return;
	}

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();
	UWorld* World = WorldPartition->GetWorld();

	const float CanvasMaxScreenSize = PartitionCanvasSize.X;
	const float GridMaxScreenSize = CanvasMaxScreenSize / FilteredStreamingGrids.Num();
	const float GridEffectiveScreenRatio = 1.f;
	const float GridEffectiveScreenSize = FMath::Min(GridMaxScreenSize, PartitionCanvasSize.Y) - 10.f;
	const float GridViewMinimumSizeInCellCount = 5.f;
	const FVector2D GridScreenExtent = FVector2D(GridEffectiveScreenSize, GridEffectiveScreenSize);
	const FVector2D GridScreenHalfExtent = 0.5f * GridScreenExtent;
	const FVector2D GridScreenInitialOffset = Offset;

	// Sort streaming grids to render them sorted by loading range
	FilteredStreamingGrids.Sort([](const FSpatialHashStreamingGrid& A, const FSpatialHashStreamingGrid& B) { return A.LoadingRange < B.LoadingRange; });

	int32 GridIndex = 0;
	for (const FSpatialHashStreamingGrid* StreamingGrid : FilteredStreamingGrids)
	{
		// Display view sides based on extended grid loading range (minimum of N cells)
		// Take into consideration GShowRuntimeSpatialHashGridLevel when using CellSize
		int32 MinGridLevel = FMath::Clamp<int32>(GShowRuntimeSpatialHashGridLevel, 0, StreamingGrid->GridLevels.Num() - 1);
		int64 CellSize = (1LL << MinGridLevel) * (int64)StreamingGrid->CellSize;
		const FVector MinExtent(CellSize * GridViewMinimumSizeInCellCount);
		FBox Region(ForceInit);
		for (const FWorldPartitionStreamingSource& Source : Sources)
		{
			Region += Source.CalcBounds(StreamingGrid->GetLoadingRange(), StreamingGrid->GridName, StreamingGrid->HLODLayer, /*bCalcIn2D*/ true);
		}
		Region += FBox(Region.GetCenter() - MinExtent, Region.GetCenter() + MinExtent);
		const FVector2D GridReferenceWorldPos = FVector2D(Region.GetCenter());
		const float RegionExtent = FVector2D(Region.GetExtent()).Size();
		const FVector2D GridScreenOffset = GridScreenInitialOffset + ((float)GridIndex * FVector2D(GridMaxScreenSize, 0.f)) + GridScreenHalfExtent;
		const FBox2D GridScreenBounds(GridScreenOffset - GridScreenHalfExtent, GridScreenOffset + GridScreenHalfExtent);
		const float WorldToScreenScale = (0.5f * GridEffectiveScreenSize) / RegionExtent;
		auto WorldToScreen = [&](const FVector2D& WorldPos) { return (WorldToScreenScale * (WorldPos - GridReferenceWorldPos)) + GridScreenOffset; };

		StreamingGrid->Draw2D(Canvas, World, Sources, Region, GridScreenBounds, WorldToScreen);

		// Draw WorldPartition name
		FVector2D GridInfoPos = GridScreenOffset - GridScreenHalfExtent;
		{
			const FString GridInfoText = UWorld::RemovePIEPrefix(FPaths::GetBaseFilename(WorldPartition->GetPackage()->GetName()));
			float TextWidth, TextHeight;
			Canvas->StrLen(GEngine->GetTinyFont(), GridInfoText, TextWidth, TextHeight);
			Canvas->SetDrawColor(255, 255, 255);
			Canvas->DrawText(GEngine->GetTinyFont(), GridInfoText, GridInfoPos.X, GridInfoPos.Y);
			GridInfoPos.Y += TextHeight + 1;
		}

		// Draw Grid name, loading range
		{
			FString GridInfoText = FString::Printf(TEXT("%s | %d m"), *StreamingGrid->GridName.ToString(), int32(StreamingGrid->GetLoadingRange() * 0.01f));
			if (StreamingGrid->bClientOnlyVisible)
			{
				GridInfoText += TEXT(" | Client Only");
			}
#if !UE_BUILD_SHIPPING
			if (GFilterRuntimeSpatialHashGridLevel != INDEX_NONE)
			{
				GridInfoText += FString::Printf(TEXT(" | GridLevelFilter %d"), GFilterRuntimeSpatialHashGridLevel);
			}
#endif
			Canvas->SetDrawColor(255, 255, 0);
			Canvas->DrawText(GEngine->GetTinyFont(), GridInfoText, GridInfoPos.X, GridInfoPos.Y);
		}

		++GridIndex;
	}
}

void UWorldPartitionRuntimeSpatialHash::Draw3D(const TArray<FWorldPartitionStreamingSource>& Sources) const
{
	UWorld* World = GetWorld();
	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	FTransform Transform = WorldPartition->GetInstanceTransform();
	TArray<const FSpatialHashStreamingGrid*> FilteredStreamingGrids = GetFilteredStreamingGrids();
	for (const FSpatialHashStreamingGrid* StreamingGrid : FilteredStreamingGrids)
	{
		StreamingGrid->Draw3D(World, Sources, Transform);
	}
}

bool UWorldPartitionRuntimeSpatialHash::ContainsRuntimeHash(const FString& Name) const
{
	for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
	{
		if (StreamingGrid.GridName.ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

TArray<const FSpatialHashStreamingGrid*> UWorldPartitionRuntimeSpatialHash::GetFilteredStreamingGrids() const
{
	TArray<const FSpatialHashStreamingGrid*> FilteredStreamingGrids;
	FilteredStreamingGrids.Reserve(StreamingGrids.Num());
	Algo::TransformIf(StreamingGrids, FilteredStreamingGrids,
		[](const FSpatialHashStreamingGrid& StreamingGrid) { return FWorldPartitionDebugHelper::IsDebugRuntimeHashGridShown(StreamingGrid.GridName); },
		[](const FSpatialHashStreamingGrid& StreamingGrid) { return &StreamingGrid; });
	return FilteredStreamingGrids;
}

#undef LOCTEXT_NAMESPACE
