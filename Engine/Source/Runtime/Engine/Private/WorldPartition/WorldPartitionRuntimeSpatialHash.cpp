// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldPartitionRuntimeSpatialHash.cpp: UWorldPartitionRuntimeSpatialHash implementation
=============================================================================*/
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHashCell.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionHandle.h"

#include "WorldPartition/RuntimeSpatialHash/RuntimeSpatialHashGridHelper.h"

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"

#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"

#include "GameFramework/WorldSettings.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "RenderUtils.h"
#include "Algo/Transform.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Misc/Parse.h"

#include "Algo/Find.h"
#include "Algo/ForEach.h"

#include "Engine/WorldComposition.h"
#include "LevelUtils.h"

extern UNREALED_API class UEditorEngine* GEditor;
#endif //WITH_EDITOR

DEFINE_LOG_CATEGORY(LogWorldPartitionRuntimeSpatialHash);

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

static int32 GShowRuntimeSpatialHashGridIndex = 0;
static FAutoConsoleVariableRef CVarShowRuntimeSpatialHashGridIndex(
	TEXT("wp.Runtime.ShowRuntimeSpatialHashGridIndex"),
	GShowRuntimeSpatialHashGridIndex,
	TEXT("Used to show only one particular grid when showing world partition runtime hash (invalid index will show all)."));

static float GRuntimeSpatialHashCellToSourceAngleContributionToCellImportance = 0.4f; // Value between [0, 1]
static FAutoConsoleVariableRef CVarRuntimeSpatialHashCellToSourceAngleContributionToCellImportance(
	TEXT("wp.Runtime.RuntimeSpatialHashCellToSourceAngleContributionToCellImportance"),
	GRuntimeSpatialHashCellToSourceAngleContributionToCellImportance,
	TEXT("Value between 0 and 1 that modulates the contribution of the angle between streaming source-to-cell vector and source-forward vector to the cell importance. The closest to 0, the less the angle will contribute to the cell importance."));

// ------------------------------------------------------------------------------------------------
FSpatialHashStreamingGrid::FSpatialHashStreamingGrid()
	: Origin(ForceInitToZero)
	, CellSize(0)
	, GridSize(0)
	, LoadingRange(0.0f)
#if WITH_EDITOR
	, DebugColor(ForceInitToZero)
	, OverrideLoadingRange(0)
#endif
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
		GridHelper = new FSquare2DGridHelper(GridLevels.Num(), Origin, CellSize, GridSize);
	}
	else
	{
		check(GridHelper->Levels.Num() == GridLevels.Num());
		check(GridHelper->Origin == Origin);
		check(GridHelper->CellSize == CellSize);
		check(GridHelper->GridSize == GridSize);
	}
	return *GridHelper;
}

void FSpatialHashStreamingGrid::GetCells(const TArray<FWorldPartitionStreamingSource>& Sources, const UDataLayerSubsystem* DataLayerSubsystem, TSet<const UWorldPartitionRuntimeCell*>& Cells) const
{
	const FSquare2DGridHelper& Helper = GetGridHelper();
	for (const FWorldPartitionStreamingSource& Source : Sources)
	{
		const FSphere GridSphere(Source.Location, GetLoadingRange());
		Helper.ForEachIntersectingCells(GridSphere, [&](const FIntVector& Coords)
		{
			const FSpatialHashStreamingGridLayerCell& LayerCell = GridLevels[Coords.Z].LayerCells[Coords.Y * Helper.Levels[Coords.Z].GridSize + Coords.X];
			for (const UWorldPartitionRuntimeCell* Cell : LayerCell.GridCells)
			{
				if (!Cell->HasDataLayers() || (DataLayerSubsystem && DataLayerSubsystem->IsAnyDataLayerActive(Cell->GetDataLayers())))
				{
					Cells.Add(Cell);
				}
			}
		});
	}

	GetAlwaysLoadedCells(DataLayerSubsystem, Cells);
}

void FSpatialHashStreamingGrid::GetAlwaysLoadedCells(const UDataLayerSubsystem* DataLayerSubsystem, TSet<const UWorldPartitionRuntimeCell*>& Cells) const
{
	if (GridLevels.Num() > 0)
	{
		const int32 TopLevel = GridLevels.Num() - 1;
		for (const FSpatialHashStreamingGridLayerCell& LayerCell : GridLevels[TopLevel].LayerCells)
		{
			for (const UWorldPartitionRuntimeCell* Cell : LayerCell.GridCells)
			{
				if (!Cell->HasDataLayers() || (DataLayerSubsystem && DataLayerSubsystem->IsAnyDataLayerActive(Cell->GetDataLayers())))
				{
					check(Cell->IsAlwaysLoaded() || Cell->HasDataLayers());
					Cells.Add(Cell);
				}
			}
		}
	}
}

void FSpatialHashStreamingGrid::Draw3D(UWorld* World, const TArray<FWorldPartitionStreamingSource>& Sources, const FTransform& Transform) const
{
	const FSquare2DGridHelper& Helper = GetGridHelper();
	int32 MinGridLevel = FMath::Clamp<int32>(GShowRuntimeSpatialHashGridLevel, 0, GridLevels.Num() - 1);
	int32 MaxGridLevel = FMath::Clamp<int32>(MinGridLevel + GShowRuntimeSpatialHashGridLevelCount - 1, 0, GridLevels.Num() - 1);
	const float GridViewMinimumSizeInCellCount = 5.f;
	const float GridViewLoadingRangeExtentRatio = 1.5f;
	const float Radius = GetLoadingRange();
	const float GridSideDistance = FMath::Max((2.f * Radius * GridViewLoadingRangeExtentRatio), CellSize * GridViewMinimumSizeInCellCount);

	for (const FWorldPartitionStreamingSource& Source : Sources)
	{
		FVector StartTrace = Source.Location + FVector(0.f, 0.f, 100.f);
		FVector EndTrace = StartTrace - FVector(0.f, 0.f, 1000000.f);
		float Z = Source.Location.Z;
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, StartTrace, EndTrace, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(DebugWorldPartitionTrace), true)))
		{
			Z = Hit.ImpactPoint.Z;
		}

		FSphere Sphere(Source.Location, GridSideDistance * 0.5f);
		const FBox Region(Sphere.Center - Sphere.W, Sphere.Center + Sphere.W);
		for (int32 GridLevel = MinGridLevel; GridLevel <= MaxGridLevel; ++GridLevel)
		{
			Helper.Levels[GridLevel].ForEachIntersectingCells(Region, [&](const FIntVector2& Coords)
			{
				FBox2D CellWorldBounds;
				Helper.Levels[GridLevel].GetCellBounds(FIntVector2(Coords.X, Coords.Y), CellWorldBounds);
				
				FVector BoundsExtent(CellWorldBounds.GetExtent(), 100.f);
				const FSpatialHashStreamingGridLayerCell& LayerCell = GridLevels[GridLevel].LayerCells[Coords.Y * Helper.Levels[GridLevel].GridSize + Coords.X];
				const UWorldPartitionRuntimeSpatialHashCell* Cell = nullptr;
				for (const UWorldPartitionRuntimeSpatialHashCell* GridCell : LayerCell.GridCells)
				{
					Cell = GridCell;
					if (!GridCell->HasDataLayers())
					{
						break;
					}
				}
				FColor CellColor = Cell ? Cell->GetDebugColor().ToFColor(false).WithAlpha(16) : FColor(0, 0, 0, 16);
				FVector BoundsOrigin(CellWorldBounds.GetCenter(), Z);
				FBox Box = FBox::BuildAABB(BoundsOrigin, BoundsExtent);
				DrawDebugSolidBox(World, Box, CellColor, Transform, false, -1.f, 255);
				FVector Pos = Transform.TransformPosition(BoundsOrigin);
				DrawDebugBox(World, Pos, BoundsExtent, Transform.GetRotation(), CellColor.WithAlpha(255), false, -1.f, 255, 10.f);
			});
		}

		// Draw Loading Ranges
		FVector SphereLocation(FVector2D(Source.Location), Z);
		SphereLocation = Transform.TransformPosition(SphereLocation);
		DrawDebugSphere(World, SphereLocation, Radius, 32, FColor::White, false, -1.f, 0, 20.f);
	}
}

void FSpatialHashStreamingGrid::Draw2D(UCanvas* Canvas, const TArray<FWorldPartitionStreamingSource>& Sources, const FBox& Region, const FBox2D& GridScreenBounds, TFunctionRef<FVector2D(const FVector2D&)> WorldToScreen) const
{
	FCanvas* CanvasObject = Canvas->Canvas;
	int32 MinGridLevel = FMath::Clamp<int32>(GShowRuntimeSpatialHashGridLevel, 0, GridLevels.Num() - 1);
	int32 MaxGridLevel = FMath::Clamp<int32>(MinGridLevel + GShowRuntimeSpatialHashGridLevelCount - 1, 0, GridLevels.Num() - 1);

	for (int32 GridLevel = MinGridLevel; GridLevel <= MaxGridLevel; ++GridLevel)
	{
		// Draw X/Y Axis
		{
			FCanvasLineItem Axis;
			Axis.LineThickness = 3;
			{
				Axis.SetColor(FLinearColor::Green);
				FVector2D LineStart = WorldToScreen(FVector2D(-1638400.f, 0.f));
				FVector2D LineEnd = WorldToScreen(FVector2D(1638400.f, 0.f));
				LineStart.X = FMath::Clamp(LineStart.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				LineStart.Y = FMath::Clamp(LineStart.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				LineEnd.X = FMath::Clamp(LineEnd.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				LineEnd.Y = FMath::Clamp(LineEnd.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				Axis.Draw(CanvasObject, LineStart, LineEnd);
			}
			{
				Axis.SetColor(FLinearColor::Red);
				FVector2D LineStart = WorldToScreen(FVector2D(0.f, -1638400.f));
				FVector2D LineEnd = WorldToScreen(FVector2D(0.f, 1638400.f));
				LineStart.X = FMath::Clamp(LineStart.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				LineStart.Y = FMath::Clamp(LineStart.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				LineEnd.X = FMath::Clamp(LineEnd.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				LineEnd.Y = FMath::Clamp(LineEnd.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				Axis.Draw(CanvasObject, LineStart, LineEnd);
			}
		}

		// Draw Grid cells at desired grid level
		const FSquare2DGridHelper& Helper = GetGridHelper();
		Helper.Levels[GridLevel].ForEachIntersectingCells(Region, [&](const FIntVector2& Coords)
		{
			FBox2D CellWorldBounds;
			Helper.Levels[GridLevel].GetCellBounds(FIntVector2(Coords.X, Coords.Y), CellWorldBounds);
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
				FString GridInfoText = FString::Printf(TEXT("X%02d_Y%02d"), Coords.X, Coords.Y);
				float TextWidth, TextHeight;
				Canvas->SetDrawColor(255, 255, 0);
				Canvas->StrLen(GEngine->GetTinyFont(), GridInfoText, TextWidth, TextHeight);
				FVector2D CellBoundsSize = CellScreenBounds.GetSize();
				if (TextWidth < CellBoundsSize.X && TextHeight < CellBoundsSize.Y)
				{
					FVector2D GridInfoPos = CellScreenBounds.GetCenter() - FVector2D(TextWidth / 2, TextHeight / 2);
					Canvas->DrawText(GEngine->GetTinyFont(), GridInfoText, GridInfoPos.X, GridInfoPos.Y);
				}
			}

			const FSpatialHashStreamingGridLayerCell& LayerCell = GridLevels[GridLevel].LayerCells[Coords.Y * Helper.Levels[GridLevel].GridSize + Coords.X];
			const UWorldPartitionRuntimeSpatialHashCell* Cell = nullptr;
			for (const UWorldPartitionRuntimeSpatialHashCell* GridCell : LayerCell.GridCells)
			{
				Cell = GridCell;
				if (!GridCell->HasDataLayers())
				{
					break;
				}
			}
			FLinearColor CellColor = Cell ? Cell->GetDebugColor() : FLinearColor(0.f, 0.f, 0.f, 0.25f);
			FCanvasTileItem Item(CellScreenBounds.Min, GWhiteTexture, CellScreenBounds.GetSize(), CellColor);
			Item.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Item);

			FCanvasBoxItem Box(CellScreenBounds.Min, CellScreenBounds.GetSize());
			Box.SetColor(CellColor);
			Box.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Box);
		});

		// Draw Loading Ranges
		float Range = GetLoadingRange();

		FCanvasLineItem LineItem;
		LineItem.LineThickness = 2;
		LineItem.SetColor(FLinearColor::White);

		for (const FWorldPartitionStreamingSource& Source : Sources)
		{
			TArray<FVector> LinePoints;
			LinePoints.SetNum(2);

			float Sin, Cos;
			FMath::SinCos(&Sin, &Cos, (63.0f / 64.0f) * 2.0f * PI);
			FVector2D LineStart(Sin * Range, Cos * Range);

			for (int32 i = 0; i < 64; i++)
			{
				FMath::SinCos(&Sin, &Cos, (i / 64.0f) * 2.0f * PI);
				FVector2D LineEnd(Sin * Range, Cos * Range);
				LineItem.Draw(CanvasObject, WorldToScreen(FVector2D(Source.Location) + LineStart), WorldToScreen(FVector2D(Source.Location) + LineEnd));
				LineStart = LineEnd;
			}

			FVector2D SourceDir = FVector2D(Source.Rotation.Vector());
			if (SourceDir.Size())
			{
				SourceDir.Normalize();
				FVector2D ConeCenter(FVector2D(Source.Location));
				LineItem.Draw(CanvasObject, WorldToScreen(ConeCenter), WorldToScreen(ConeCenter + SourceDir * Range));
			}
		}

		FCanvasBoxItem Box(GridScreenBounds.Min, GridScreenBounds.GetSize());
		Box.SetColor(DebugColor);
		Canvas->DrawItem(Box);
	}
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
{}

#if WITH_EDITOR
FName UWorldPartitionRuntimeSpatialHash::GetActorRuntimeGrid(const AActor* Actor) const
{
	if (ULevel* Level = Actor ? Actor->GetLevel() : nullptr)
	{
		if (const FName* ActorRuntimeGrid = WorldCompositionStreamingLevelToRuntimeGrid.Find(FLevelUtils::FindStreamingLevel(Level)))
		{
			return *ActorRuntimeGrid;
		}
	}
	return Super::GetActorRuntimeGrid(Actor);
}

void UWorldPartitionRuntimeSpatialHash::SetDefaultValues()
{
	FSpatialHashRuntimeGrid& MainGrid = Grids.AddDefaulted_GetRef();
	MainGrid.GridName = TEXT("MainGrid");
	MainGrid.CellSize = 3200;
	MainGrid.LoadingRange = 25600;
	MainGrid.DebugColor = FLinearColor::Gray;
}

void UWorldPartitionRuntimeSpatialHash::ImportFromWorldComposition(UWorldComposition* WorldComposition)
{
	check(IsRunningCommandlet());

	if (WorldComposition)
	{
		const TArray<FWorldTileLayer> WorldCompositionTileLayers = WorldComposition->GetDistanceDependentLayers();
		for (const FWorldTileLayer& Layer : WorldCompositionTileLayers)
		{
			FName GridName = FName(Layer.Name);
			FSpatialHashRuntimeGrid* Grid = Algo::FindByPredicate(Grids, [GridName](const FSpatialHashRuntimeGrid& Grid) { return Grid.GridName == GridName; });
			if (!Grid)
			{
				Grid = &Grids.AddDefaulted_GetRef();
				Grid->GridName = GridName;
				Grid->CellSize = 3200;
				Grid->DebugColor = FLinearColor::MakeRandomColor();
			}
			// World Composition Layer Streaming Distance always wins over existing value (config file)
			Grid->LoadingRange = Layer.StreamingDistance;
		}

		const UWorldComposition::FTilesList& Tiles = WorldComposition->GetTilesList();
		for (int32 TileIdx = 0; TileIdx < Tiles.Num(); TileIdx++)
		{
			const FWorldCompositionTile& Tile = Tiles[TileIdx];
			ULevelStreaming* StreamingLevel = WorldComposition->TilesStreaming[TileIdx];
			if (StreamingLevel && WorldComposition->IsDistanceDependentLevel(Tile.PackageName))
			{
				// Map WorldComposition tiles streaming level to Runtime Grid
				WorldCompositionStreamingLevelToRuntimeGrid.Add(StreamingLevel, FName(Tile.Info.Layer.Name));
			}
		}
	}
}

bool UWorldPartitionRuntimeSpatialHash::GenerateStreaming(EWorldPartitionStreamingMode Mode, UWorldPartitionStreamingPolicy* StreamingPolicy, TArray<FString>* OutPackagesToGenerate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::GenerateStreaming);
	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	UE_SCOPED_TIMER(TEXT("GenerateStreaming"), LogWorldPartitionRuntimeSpatialHash);
	
	if (!Grids.Num())
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHash, Error, TEXT("Invalid partition grids setup"));
		return false;
	}

	TArray<FSpatialHashRuntimeGrid> AllGrids;
	AllGrids.Append(Grids);

	// Append grids from runtime grid actors
	for (AActor* Actor : GetWorld()->PersistentLevel->Actors)
	{
		if (ASpatialHashRuntimeGridInfo* RuntimeGridActor = Cast<ASpatialHashRuntimeGridInfo>(Actor))
		{
			AllGrids.Add(RuntimeGridActor->GridSettings);
		}
	}

	check(!StreamingGrids.Num());

	// Build a map of Actor GUID -> HLODActor GUID once instead of having to recompute for every streaming grid we create
	CacheHLODParents();

	TMap<FName, int32> GridsMapping;
	GridsMapping.Add(NAME_None, 0);
	for (int32 i = 0; i < AllGrids.Num(); i++)
	{
		const FSpatialHashRuntimeGrid& Grid = AllGrids[i];
		check(!GridsMapping.Contains(Grid.GridName));
		GridsMapping.Add(Grid.GridName, i);
	}

	// Create actor clusters
	TArray<FActorCluster> ActorClusters = CreateActorClusters(WorldPartition);

	TArray<TArray<FActorCluster>> GridActors;
	GridActors.InsertDefaulted(0, AllGrids.Num());

	for (FActorCluster& ActorCluster : ActorClusters)
	{
		int32* FoundIndex = GridsMapping.Find(ActorCluster.RuntimeGrid);
		if (!FoundIndex)
		{
			UE_LOG(LogWorldPartitionRuntimeSpatialHash, Error, TEXT("Invalid partition grid '%s' referenced by actor cluster"), *ActorCluster.RuntimeGrid.ToString());
		}

		int32 GridIndex = FoundIndex ? *FoundIndex : 0;
		GridActors[GridIndex].Add(MoveTemp(ActorCluster));
	}
	
	const FBox WorldBounds = WorldPartition->GetWorldBounds();
	for (int32 GridIndex=0; GridIndex < AllGrids.Num(); GridIndex++)
	{
		const FSpatialHashRuntimeGrid& Grid = AllGrids[GridIndex];
		const FSquare2DGridHelper PartionedActors = GetPartitionedActors(WorldPartition, WorldBounds, Grid, GridActors[GridIndex]);
		if (!CreateStreamingGrid(Grid, PartionedActors, Mode, StreamingPolicy, OutPackagesToGenerate))
		{
			return false;
		}
	}

	return true;
}

FName UWorldPartitionRuntimeSpatialHash::GetCellName(UWorldPartition* WorldPartition, FName InGridName, int32 InLevel, int32 InCellX, int32 InCellY, const FDataLayersID& InDataLayerID)
{
	const FString PackageName = FPackageName::GetShortName(WorldPartition->GetPackage());
	const FString PackageNameNoPIEPrefix = UWorld::RemovePIEPrefix(PackageName);

	return FName(*FString::Printf(TEXT("WPRT_%s_%s_Cell_L%d_X%02d_Y%02d_DL%X"), *PackageNameNoPIEPrefix, *InGridName.ToString(), InLevel, InCellX, InCellY, InDataLayerID.GetHash()));
}

FName UWorldPartitionRuntimeSpatialHash::GetCellName(FName InGridName, int32 InLevel, int32 InCellX, int32 InCellY, const FDataLayersID& InDataLayerID) const
{
	UWorldPartition* WorldPartition = GetOuterUWorldPartition();
	return UWorldPartitionRuntimeSpatialHash::GetCellName(WorldPartition, InGridName, InLevel, InCellX, InCellY, InDataLayerID);
}

void UWorldPartitionRuntimeSpatialHash::CacheHLODParents()
{
	CachedHLODParents.Reset();

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	for (UActorDescContainer::TIterator HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		if (HLODIterator->GetActorClass()->IsChildOf<AWorldPartitionHLOD>())
		{
			FHLODActorDesc* HLODActorDesc = (FHLODActorDesc*)*HLODIterator;
			for (const auto& SubActor : HLODActorDesc->GetSubActors())
			{
				CachedHLODParents.Emplace(SubActor, HLODActorDesc->GetGuid());
			}
		}
	}
}

bool UWorldPartitionRuntimeSpatialHash::CreateStreamingGrid(const FSpatialHashRuntimeGrid& RuntimeGrid, const FSquare2DGridHelper& PartionedActors, EWorldPartitionStreamingMode Mode, UWorldPartitionStreamingPolicy* StreamingPolicy, TArray<FString>* OutPackagesToGenerate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::CreateStreamingGrid);

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	check(FMath::IsPowerOfTwo(PartionedActors.GridSize));
	FSpatialHashStreamingGrid& CurrentStreamingGrid = StreamingGrids.AddDefaulted_GetRef();
	CurrentStreamingGrid.GridName = RuntimeGrid.GridName;
	CurrentStreamingGrid.Origin = PartionedActors.Origin;
	CurrentStreamingGrid.CellSize = PartionedActors.CellSize;
	CurrentStreamingGrid.GridSize = PartionedActors.GridSize;
	CurrentStreamingGrid.LoadingRange = RuntimeGrid.LoadingRange;
	CurrentStreamingGrid.DebugColor = RuntimeGrid.DebugColor;

	// Move actors into the final streaming grids
	CurrentStreamingGrid.GridLevels.Reserve(PartionedActors.Levels.Num());

	TArray<FGuid> FilteredActors;
	int32 Level = INDEX_NONE;
	for (const FSquare2DGridHelper::FGridLevel& TempLevel : PartionedActors.Levels)
	{
		Level++;

		FSpatialHashStreamingGridLevel& GridLevel = CurrentStreamingGrid.GridLevels.AddDefaulted_GetRef();

		GridLevel.LayerCells.SetNum(TempLevel.Cells.Num());

		int32 CellIndex = INDEX_NONE;
		for (const FSquare2DGridHelper::FGridLevel::FGridCell& TempCell : TempLevel.Cells)
		{
			CellIndex++;
			const int32 CellCoordX = CellIndex % TempLevel.GridSize;
			const int32 CellCoordY = CellIndex / TempLevel.GridSize;
			TArray<UWorldPartitionRuntimeSpatialHashCell*>& GridCells = GridLevel.LayerCells[CellIndex].GridCells;

			for (const FSquare2DGridHelper::FGridLevel::FGridCellDataChunk& GridCellDataChunk : TempCell.GetDataChunks())
			{
				FilteredActors.SetNum(0, false);
				FilteredActors.Reset(GridCellDataChunk.GetActors().Num());
				if (GridCellDataChunk.GetActors().Num())
				{
					Algo::TransformIf(GridCellDataChunk.GetActors(), FilteredActors, [&WorldPartition](const FGuid& ActorGuid)
					{
						const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(ActorGuid);
						const bool bShouldStripActorFromStreaming = ActorDesc->GetActorIsEditorOnly();
						UE_CLOG(bShouldStripActorFromStreaming, LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("Stripping Actor %s (%s) from streaming grid"), *(ActorDesc->GetActorPath().ToString()), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid));
						return !bShouldStripActorFromStreaming;
					}, [](const FGuid& ActorGuid) { return ActorGuid; });
				}

				if (!FilteredActors.Num())
				{
					continue;
				}
				
				// Cell cannot be treated as always loaded if it has data layers
				const bool bIsCellAlwaysLoaded = (&TempCell == &PartionedActors.GetAlwaysLoadedCell()) && !GridCellDataChunk.HasDataLayers();

				FName CellName = GetCellName(CurrentStreamingGrid.GridName, Level, CellCoordX, CellCoordY, GridCellDataChunk.GetDataLayersID());

				UWorldPartitionRuntimeSpatialHashCell* StreamingCell = NewObject<UWorldPartitionRuntimeSpatialHashCell>(WorldPartition, StreamingPolicy->GetRuntimeCellClass(), CellName);
				GridCells.Add(StreamingCell);
				StreamingCell->SetIsAlwaysLoaded(bIsCellAlwaysLoaded);
				StreamingCell->SetDataLayers(GridCellDataChunk.GetDataLayers());
				StreamingCell->Level = Level;
				FBox2D Bounds;
				verify(TempLevel.GetCellBounds(FIntVector2(CellCoordX, CellCoordY), Bounds));
				StreamingCell->Position = FVector(Bounds.GetCenter(), 0.f);

				UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("Cell%s %s Actors = %d"), bIsCellAlwaysLoaded ? TEXT(" (AlwaysLoaded)") : TEXT(""), *StreamingCell->GetName(), FilteredActors.Num());

				// Keep track of all AWorldPartitionHLOD actors referenced by this cell
				TSet<FGuid> ReferencedHLODActors;

				for (const FGuid& ActorGuid : FilteredActors)
				{
					const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(ActorGuid);
					FGuid ParentHLOD = CachedHLODParents.FindRef(ActorGuid);
					if (ParentHLOD.IsValid())
					{
						ReferencedHLODActors.Add(ParentHLOD);
					}
					StreamingCell->AddActorToCell(ActorGuid, ActorDesc->GetActorPackage(), ActorDesc->GetActorPath());
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("  Actor : %s (%s) Origin(%s)"), *(ActorDesc->GetActorPath().ToString()), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid), *FVector2D(ActorDesc->GetOrigin()).ToString());
				}

				if (ReferencedHLODActors.Num() > 0)
				{
					// Store the referenced HLOD actors as custom cell data
					UWorldPartitionRuntimeHLODCellData* HLODCellData = NewObject<UWorldPartitionRuntimeHLODCellData>(StreamingCell);
					HLODCellData->SetReferencedHLODActors(ReferencedHLODActors.Array());
					StreamingCell->AddCellData(HLODCellData);
				}

				if (Mode == EWorldPartitionStreamingMode::RuntimeStreamingCells)
				{
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Log, TEXT("Creating runtime streaming cells %s."), *StreamingCell->GetName());

					if (StreamingCell->GetActorCount())
					{
						// Always loaded cell actors are transfered to World's Persistent Level
						if (StreamingCell->IsAlwaysLoaded())
						{
							StreamingCell->MoveAlwaysLoadedContentToPersistentLevel();
						}
						else
						{
							if (!OutPackagesToGenerate)
							{
								UE_LOG(LogWorldPartitionRuntimeSpatialHash, Error, TEXT("Error creating runtime streaming cells for cook, OutPackagesToGenerate is null."));
								return false;
							}

							const FString PackageRelativePath = StreamingCell->GetPackageNameToCreate();
							check(!PackageRelativePath.IsEmpty());
							OutPackagesToGenerate->Add(PackageRelativePath);

							// Map relative package to StreamingCell for PopulateGeneratedPackageForCook/FinalizeGeneratedPackageForCook
							PackagesToGenerateForCook.Add(PackageRelativePath, StreamingCell);
						}
					}
				}
			}
		}
	}

	return true;
}

bool UWorldPartitionRuntimeSpatialHash::PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath, const FString& InPackageCookName)
{
	if (UWorldPartitionRuntimeCell** MatchingCell = PackagesToGenerateForCook.Find(InPackageRelativePath))
	{
		UWorldPartitionRuntimeCell* Cell = *MatchingCell;
		if (ensure(Cell))
		{
			return Cell->PopulateGeneratedPackageForCook(InPackage, InPackageCookName);
		}
	}
	return false;
}

void UWorldPartitionRuntimeSpatialHash::FinalizeGeneratedPackageForCook()
{
	for (const auto& Package : PackagesToGenerateForCook)
	{
		UWorldPartitionRuntimeCell* Cell = Package.Value;
		if (ensure(Cell))
		{
			Cell->FinalizeGeneratedPackageForCook();
		}
	}
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
		float OverrideLoadingRange = 0.f;
		FParse::Value(*ArgString, TEXT("grid="), GridIndex);
		FParse::Value(*ArgString, TEXT("range="), OverrideLoadingRange);

		if (OverrideLoadingRange > 0.f)
		{
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
		}
	})
);

// Streaming interface
int32 UWorldPartitionRuntimeSpatialHash::GetAllStreamingCells(TSet<const UWorldPartitionRuntimeCell*>& Cells, bool bIncludeDataLayers /*=false*/) const
{
	for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
	{
		for (const FSpatialHashStreamingGridLevel& GridLevel : StreamingGrid.GridLevels)
		{
			for (const FSpatialHashStreamingGridLayerCell& LayerCell : GridLevel.LayerCells)
			{
				for (const UWorldPartitionRuntimeSpatialHashCell* Cell : LayerCell.GridCells)
				{
					if (bIncludeDataLayers || !Cell->HasDataLayers())
					{
						Cells.Add(Cell);
					}
				}
			}
		}
	}

	return Cells.Num();
}

int32 UWorldPartitionRuntimeSpatialHash::GetStreamingCells(const TArray<FWorldPartitionStreamingSource>& Sources, TSet<const UWorldPartitionRuntimeCell*>& Cells) const
{
	UDataLayerSubsystem* DataLayerSubsystem = GetWorld()->GetSubsystem<UDataLayerSubsystem>();

	if (Sources.Num() == 0)
	{
		// Get always loaded cells
		for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
		{
			StreamingGrid.GetAlwaysLoadedCells(DataLayerSubsystem, Cells);
		}
	}
	else
	{
		// Get cells based on streaming sources
		for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
		{
			StreamingGrid.GetCells(Sources, DataLayerSubsystem, Cells);
		}
	}

	return Cells.Num();
}

void UWorldPartitionRuntimeSpatialHash::SortStreamingCellsByImportance(const TSet<const UWorldPartitionRuntimeCell*>& InCells, const TArray<FWorldPartitionStreamingSource>& InSources, TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>>& OutSortedCells) const
{
	struct FCellShortestDist
	{
		FCellShortestDist(const UWorldPartitionRuntimeSpatialHashCell* InCell, float InMinDistance)
			: Cell(InCell)
			, SourceMinDistance(InMinDistance)
		{}
		const UWorldPartitionRuntimeSpatialHashCell* Cell;
		float SourceMinDistance;
	};

	TArray<FCellShortestDist> SortedCells;
	SortedCells.Reserve(InCells.Num());
	for (const UWorldPartitionRuntimeCell* ToLoadCell : InCells)
	{
		const UWorldPartitionRuntimeSpatialHashCell* Cell = Cast<const UWorldPartitionRuntimeSpatialHashCell>(ToLoadCell);
		FCellShortestDist& SortedCell = SortedCells.Emplace_GetRef(Cell, FLT_MAX);

		const float AngleContribution = FMath::Clamp(GRuntimeSpatialHashCellToSourceAngleContributionToCellImportance, 0.f, 1.f);
		for (const FWorldPartitionStreamingSource& Source : InSources)
		{
			const float SqrDistance = FVector::DistSquared(Source.Location, Cell->Position);
			float AngleFactor = 1.f;
			if (!FMath::IsNearlyZero(AngleContribution))
			{
				const FVector2D SourceForward(Source.Rotation.Quaternion().GetForwardVector());
				const FVector2D SourceToCell(Cell->Position - Source.Location);
				const float Dot = FVector2D::DotProduct(SourceForward.GetSafeNormal(), SourceToCell.GetSafeNormal());
				const float NormalizedAngle = FMath::Clamp(FMath::Abs(FMath::Acos(Dot)/PI), 0.f, 1.f);
				AngleFactor = FMath::Pow(NormalizedAngle, AngleContribution);
			}
			// Modulate distance to cell by angle relative to source forward vector (to prioritize cells in front)
			SortedCell.SourceMinDistance = FMath::Min(SqrDistance * AngleFactor, SortedCell.SourceMinDistance);
		}
	}

	Algo::Sort(SortedCells, [](const FCellShortestDist& A, const FCellShortestDist& B)
	{
		if (A.Cell->Level == B.Cell->Level)
		{
			return A.SourceMinDistance < B.SourceMinDistance;
		}
		return A.Cell->Level > B.Cell->Level;
	});

	OutSortedCells.Reserve(InCells.Num());
	for (const FCellShortestDist& SortedCell : SortedCells)
	{
		OutSortedCells.Add(SortedCell.Cell);
	}
}

FVector2D UWorldPartitionRuntimeSpatialHash::GetDraw2DDesiredFootprint(const FVector2D& CanvasSize) const
{
	return FVector2D(CanvasSize.X * StreamingGrids.Num(), CanvasSize.Y);
}

void UWorldPartitionRuntimeSpatialHash::Draw2D(UCanvas* Canvas, const TArray<FWorldPartitionStreamingSource>& Sources, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize) const
{
	if (StreamingGrids.Num() == 0 || Sources.Num() == 0)
	{
		return;
	}

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	const float CanvasMaxScreenSize = PartitionCanvasSize.X;
	const float GridMaxScreenSize = CanvasMaxScreenSize / StreamingGrids.Num();
	const float GridEffectiveScreenRatio = 1.f;
	const float GridEffectiveScreenSize = FMath::Min(GridMaxScreenSize, PartitionCanvasSize.Y) - 10.f;
	const float GridViewLoadingRangeExtentRatio = 1.5f;
	const float GridViewMinimumSizeInCellCount = 5.f;
	const FVector2D GridScreenExtent = FVector2D(GridEffectiveScreenSize, GridEffectiveScreenSize);
	const FVector2D GridScreenHalfExtent = 0.5f * GridScreenExtent;
	const FVector2D GridScreenInitialOffset = PartitionCanvasOffset;

	// Sort streaming grids to render them sorted by loading range
	TArray<const FSpatialHashStreamingGrid*> SortedStreamingGrids;
	Algo::Transform(StreamingGrids, SortedStreamingGrids, [](const FSpatialHashStreamingGrid& StreamingGrid) { return &StreamingGrid; });
	SortedStreamingGrids.Sort([](const FSpatialHashStreamingGrid& A, const FSpatialHashStreamingGrid& B) { return A.LoadingRange < B.LoadingRange; });

	int32 GridIndex = 0;
	for (const FSpatialHashStreamingGrid* StreamingGrid : SortedStreamingGrids)
	{
		// Display view sides based on extended grid loading range (minimum of N cells)
		const float GridSideDistance = FMath::Max((2.f * StreamingGrid->GetLoadingRange() * GridViewLoadingRangeExtentRatio), StreamingGrid->CellSize * GridViewMinimumSizeInCellCount);
		FSphere AverageSphere(ForceInit);
		for (const FWorldPartitionStreamingSource& Source : Sources)
		{
			AverageSphere += FSphere(Source.Location, 0.5f * GridSideDistance);
		}
		const FVector2D GridReferenceWorldPos = FVector2D(AverageSphere.Center);
		const FBox Region(AverageSphere.Center - AverageSphere.W, AverageSphere.Center + AverageSphere.W);
		const FVector2D GridScreenOffset = GridScreenInitialOffset + ((float)GridIndex * FVector2D(GridMaxScreenSize, 0.f)) + GridScreenHalfExtent;
		const FBox2D GridScreenBounds(GridScreenOffset - GridScreenHalfExtent, GridScreenOffset + GridScreenHalfExtent);
		const float WorldToScreenScale = (0.5f * GridEffectiveScreenSize) / AverageSphere.W;
		auto WorldToScreen = [&](const FVector2D& WorldPos) { return (WorldToScreenScale * (WorldPos - GridReferenceWorldPos)) + GridScreenOffset; };

		StreamingGrid->Draw2D(Canvas, Sources, Region, GridScreenBounds, WorldToScreen);

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

	if (StreamingGrids.IsValidIndex(GShowRuntimeSpatialHashGridIndex))
	{
		StreamingGrids[GShowRuntimeSpatialHashGridIndex].Draw3D(World, Sources, Transform);
	}
	else
	{
		for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
		{
			StreamingGrid.Draw3D(World, Sources, Transform);
		}
	}
}