// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/SWorldPartitionEditorGridSpatialHash.h"
#include "GameFramework/WorldSettings.h"
#include "DerivedDataCache/Public/DerivedDataCacheInterface.h"
#include "Misc/SecureHash.h"
#include "WorldPartition/WorldPartitionEditorCell.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "WorldPartition/WorldPartitionEditorSpatialHash.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionMiniMap.h"
#include "WorldPartition/WorldPartitionMiniMapHelper.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#include "Framework/Commands/Commands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateTextures.h"
#include "Brushes/SlateColorBrush.h"
#include "ObjectTools.h"
#include "UObject/ObjectResource.h"
#include "EditorStyleSet.h"
#include "Selection.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DDynamic.h"
#include "Misc/HashBuilder.h"
#include "Fonts/FontMeasure.h"
#include "EngineModule.h"
#include "Renderer/Private/RendererModule.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

SWorldPartitionEditorGridSpatialHash::SWorldPartitionEditorGridSpatialHash()
	: WorldMiniMapBounds(ForceInit)
{
}

SWorldPartitionEditorGridSpatialHash::~SWorldPartitionEditorGridSpatialHash()
{
}

void SWorldPartitionEditorGridSpatialHash::Construct(const FArguments& InArgs)
{
	World = InArgs._InWorld;
	WorldPartition = World ? World->GetWorldPartition() : nullptr;

	if (WorldPartition)
	{
		UpdateWorldMiniMapDetails();

		bShowActors = !WorldMiniMapBrush.HasUObject();
	}

	SWorldPartitionEditorGrid2D::Construct(SWorldPartitionEditorGrid::FArguments().InWorld(InArgs._InWorld));
}

void SWorldPartitionEditorGridSpatialHash::UpdateWorldMiniMapDetails()
{
	AWorldPartitionMiniMap* WorldMiniMap = FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World);
	if (WorldMiniMap)
	{
		WorldMiniMapBounds = FBox2D(FVector2D(WorldMiniMap->MiniMapWorldBounds.Min), FVector2D(WorldMiniMap->MiniMapWorldBounds.Max));
		if (UTexture2D* MiniMapTexture = WorldMiniMap->MiniMapTexture)
		{
			WorldMiniMapBrush.SetUVRegion(WorldMiniMap->UVOffset);
			WorldMiniMapBrush.SetImageSize(MiniMapTexture->GetImportedSize());
			WorldMiniMapBrush.SetResourceObject(MiniMapTexture);
		}
	}
}

int32 SWorldPartitionEditorGridSpatialHash::PaintGrid(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FBox2D ViewRect(FVector2D(ForceInitToZero), AllottedGeometry.GetLocalSize());
	const FBox2D ViewRectWorld(ScreenToWorld.TransformPoint(ViewRect.Min), ScreenToWorld.TransformPoint(ViewRect.Max));

	UWorldPartitionEditorSpatialHash* EditorSpatialHash = (UWorldPartitionEditorSpatialHash*)WorldPartition->EditorHash;
	
	FBox VisibleGridRectWorld(
		FVector(
			FMath::Max(FMath::FloorToFloat(EditorSpatialHash->Bounds.Min.X / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize, ViewRectWorld.Min.X),
			FMath::Max(FMath::FloorToFloat(EditorSpatialHash->Bounds.Min.Y / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize, ViewRectWorld.Min.Y),
			FMath::FloorToFloat(EditorSpatialHash->Bounds.Min.Z / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize
		),
		FVector(
			FMath::Min(FMath::FloorToFloat(EditorSpatialHash->Bounds.Max.X / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize, ViewRectWorld.Max.X),
			FMath::Min(FMath::FloorToFloat(EditorSpatialHash->Bounds.Max.Y / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize, ViewRectWorld.Max.Y),
			FMath::FloorToFloat(EditorSpatialHash->Bounds.Max.Z / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize
		)
	);

	// Shadow whole grid area
	{
		FSlateColorBrush ShadowBrush(FLinearColor::Black);
		FLinearColor ShadowColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));

		FPaintGeometry GridGeometry = AllottedGeometry.ToPaintGeometry(
			WorldToScreen.TransformPoint(FVector2D(VisibleGridRectWorld.Min)),
			WorldToScreen.TransformPoint(FVector2D(VisibleGridRectWorld.Max)) - WorldToScreen.TransformPoint(FVector2D(VisibleGridRectWorld.Min))
		);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			++LayerId,
			GridGeometry,
			&ShadowBrush,
			ESlateDrawEffect::None,
			ShadowColor
		);
	}

	// Draw MiniMap image if any
	if (UTexture2D* Texture2D = Cast<UTexture2D>(WorldMiniMapBrush.GetResourceObject()))
	{	
		FPaintGeometry WorldImageGeometry = AllottedGeometry.ToPaintGeometry(
			WorldToScreen.TransformPoint(WorldMiniMapBounds.Min),
			WorldToScreen.TransformPoint(WorldMiniMapBounds.Max) - WorldToScreen.TransformPoint(WorldMiniMapBounds.Min)
		);

		FSlateDrawElement::MakeRotatedBox(
			OutDrawElements,
			++LayerId,
			WorldImageGeometry,
			&WorldMiniMapBrush
		);

		if (Texture2D->IsCurrentlyVirtualTextured())
		{
			FVirtualTexture2DResource* VTResource = static_cast<FVirtualTexture2DResource*>(Texture2D->GetResource());
			const FVector2D ViewportSize = AllottedGeometry.GetLocalSize();
			const FVector2D ScreenSpaceSize = WorldImageGeometry.GetLocalSize();
			const FVector2D ViewportPositon = -WorldImageGeometry.GetAccumulatedRenderTransform().GetTranslation() + AllottedGeometry.GetAbsolutePosition();

			const FVector2D UV0 = WorldMiniMapBrush.GetUVRegion().Min;
			const FVector2D UV1 = WorldMiniMapBrush.GetUVRegion().Max;

			const ERHIFeatureLevel::Type InFeatureLevel = GMaxRHIFeatureLevel;
			const int32 MipLevel = -1;

			ENQUEUE_RENDER_COMMAND(MakeTilesResident)(
				[InFeatureLevel, VTResource, ScreenSpaceSize, ViewportPositon, ViewportSize, UV0, UV1, MipLevel](FRHICommandListImmediate& RHICmdList)
			{
				// AcquireAllocatedVT() must happen on render thread
				IAllocatedVirtualTexture* AllocatedVT = VTResource->AcquireAllocatedVT();

				IRendererModule& RenderModule = GetRendererModule();
				RenderModule.RequestVirtualTextureTilesForRegion(AllocatedVT, ScreenSpaceSize, ViewportPositon, ViewportSize, UV0, UV1, MipLevel);
				RenderModule.LoadPendingVirtualTextureTiles(RHICmdList, InFeatureLevel);
			});
		}
	}

	struct FCellDesc2D
	{
		FCellDesc2D()
			: Bounds(ForceInitToZero)
			, NumLoaded(0)
			, NumUnloaded(0)
			, NumEmpty(0)
		{}

		FBox2D Bounds;
		int32 NumLoaded;
		int32 NumUnloaded;
		int32 NumEmpty;
	};

	// Draw shadowed regions
	{
		TMap<uint32, FCellDesc2D> FlattenedCells2D;
		EditorSpatialHash->ForEachIntersectingCells(VisibleGridRectWorld.ExpandBy(-1), 0, [&](const UWorldPartitionEditorSpatialHash::FCellCoord& CellCoord)
		{
			FHashBuilder HashBuilder;
			HashBuilder << CellCoord.X << CellCoord.Y;
			uint32 CellHash2D = HashBuilder.GetHash();			
			
			FCellDesc2D& CellDesc2D = FlattenedCells2D.FindOrAdd(CellHash2D);
			
			FBox CellBounds = EditorSpatialHash->GetCellBounds(CellCoord);
			CellDesc2D.Bounds = FBox2D(FVector2D(CellBounds.Min), FVector2D(CellBounds.Max));

			if (UWorldPartitionEditorCell** CellPtr = EditorSpatialHash->HashCells.Find(CellCoord))
			{
				if ((*CellPtr)->IsEmpty())
				{
					CellDesc2D.NumEmpty++;
				}
				else if ((*CellPtr)->IsLoaded())
				{
					CellDesc2D.NumLoaded++;
				}
				else
				{
					CellDesc2D.NumUnloaded++;
				}
			}
			else
			{
				CellDesc2D.NumEmpty++;
			}
		});

		for(auto& UniqueCell: FlattenedCells2D)
		{
			const FCellDesc2D& Cell = UniqueCell.Value;

			// Fully loaded
			if (Cell.NumLoaded && !Cell.NumUnloaded)
			{
				continue;
			}

			const FPaintGeometry CellGeometry = AllottedGeometry.ToPaintGeometry(
				WorldToScreen.TransformPoint(Cell.Bounds.Min),
				WorldToScreen.TransformPoint(Cell.Bounds.Max) - WorldToScreen.TransformPoint(Cell.Bounds.Min)
			);

			const FSlateColorBrush CellBrush(FLinearColor::White);
			const FLinearColor FullyUnloadedCellColor(0, 0, 0, 0.5f);
			const FLinearColor PartiallyLoadedCellColor(0, 0, 0, 0.25f);
			const FLinearColor EmptyCellColor(0, 0, 0, 0.75f);
			
			FLinearColor CellColor;

			// Partially loaded aread
			if (Cell.NumLoaded && Cell.NumUnloaded)
			{
				CellColor = PartiallyLoadedCellColor;
			}
			else if (Cell.NumEmpty && !Cell.NumUnloaded && !Cell.NumLoaded)
			{
				CellColor = EmptyCellColor;
			}
			else
			{
				CellColor = FullyUnloadedCellColor;
			}

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				++LayerId,
				CellGeometry,
				&CellBrush,
				ESlateDrawEffect::None,
				CellColor
			);
		}
	}

	// Draw selected cells
	if (SelectBox.IsValid)
	{
		const FBox VisibleSelectBox = SelectBox.Overlap(VisibleGridRectWorld);

		if (VisibleSelectBox.IsValid)
		{
			TMap<uint32, FCellDesc2D> FlattenedCells2D;
			WorldPartition->EditorHash->ForEachIntersectingCell(VisibleSelectBox, [&](UWorldPartitionEditorCell* Cell)
			{
				UWorldPartitionEditorSpatialHash::FCellCoord CellCoord = EditorSpatialHash->GetCellCoords(Cell->Bounds.GetCenter(), 0);

				FHashBuilder HashBuilder;
				HashBuilder << CellCoord.X << CellCoord.Y;
				uint32 CellHash2D = HashBuilder.GetHash();
				FCellDesc2D& CellDesc2D = FlattenedCells2D.Add(CellHash2D);

				CellDesc2D.Bounds = FBox2D(FVector2D(Cell->Bounds.Min), FVector2D(Cell->Bounds.Max));
			});

			for (auto& UniqueCell : FlattenedCells2D)
			{
				const FCellDesc2D& Cell = UniqueCell.Value;

				FPaintGeometry CellGeometry = AllottedGeometry.ToPaintGeometry(
					WorldToScreen.TransformPoint(Cell.Bounds.Min),
					WorldToScreen.TransformPoint(Cell.Bounds.Max) - WorldToScreen.TransformPoint(Cell.Bounds.Min)
				);

				FSlateColorBrush CellBrush(FLinearColor::White);
				FLinearColor CellColor(1, 1, 1, 0.25f);

				FSlateDrawElement::MakeBox(
					OutDrawElements,
					++LayerId,
					CellGeometry,
					&CellBrush,
					ESlateDrawEffect::None,
					CellColor
				);
			}
		}
	}

	if (FBox2D(FVector2D(VisibleGridRectWorld.Min), FVector2D(VisibleGridRectWorld.Max)).GetArea() > 0.0f)
	{
		const FLinearColor Color = FLinearColor(0.1f, 0.1f, 0.1f, 1.f);

		FIntVector2 TopLeftW(
			FMath::FloorToFloat(VisibleGridRectWorld.Min.X / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize,
			FMath::FloorToFloat(VisibleGridRectWorld.Min.Y / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize
		);

		FIntVector2 BottomRightW(
			FMath::CeilToFloat(VisibleGridRectWorld.Max.X / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize,
			FMath::CeilToFloat(VisibleGridRectWorld.Max.Y / EditorSpatialHash->CellSize) * EditorSpatialHash->CellSize
		);

		TArray<FVector2D> LinePoints;
		LinePoints.SetNum(2);

		// Horizontal
		for (int32 i=TopLeftW.Y; i<=BottomRightW.Y; i+=EditorSpatialHash->CellSize)
		{
			FVector2D LineStartH(TopLeftW.X, i);
			FVector2D LineEndH(BottomRightW.X, i);

			LinePoints[0] = WorldToScreen.TransformPoint(LineStartH);
			LinePoints[1] = WorldToScreen.TransformPoint(LineEndH);

			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::NoBlending, Color, false, 1.0f);
		}

		// Vertical
		for (int32 i=TopLeftW.X; i<=BottomRightW.X; i+=EditorSpatialHash->CellSize)
		{
			FVector2D LineStartH(i, TopLeftW.Y);
			FVector2D LineEndH(i, BottomRightW.Y);

			LinePoints[0] = WorldToScreen.TransformPoint(LineStartH);
			LinePoints[1] = WorldToScreen.TransformPoint(LineEndH);

			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::NoBlending, Color, false, 1.0f);
		}

		// Draw coordinates
		if (GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetShowCellCoords())
		{
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FVector2D CellScreenSize = WorldToScreen.TransformVector(FVector2D(EditorSpatialHash->CellSize, EditorSpatialHash->CellSize));

			FSlateFontInfo CoordsFont;
			FVector2D DefaultCoordTextSize;
			bool bNeedsGradient = true;
			for(int32 DesiredFontSize = 24; DesiredFontSize >= 8; DesiredFontSize -= 2)
			{
				CoordsFont = FCoreStyle::GetDefaultFontStyle("Bold", DesiredFontSize);
				DefaultCoordTextSize = FontMeasure->Measure(TEXT("(-99,-99)"), CoordsFont);				

				if (CellScreenSize.X > DefaultCoordTextSize.X)
				{
					bNeedsGradient = (DesiredFontSize == 8);
					break;
				}
			}

			if (CellScreenSize.X > DefaultCoordTextSize.X)
			{
				static float GradientDistance = 64.0f;
				float ColorGradient = bNeedsGradient ? FMath::Min((CellScreenSize.X - DefaultCoordTextSize.X) / GradientDistance, 1.0f) : 1.0f;
				const FLinearColor CoordTextColor(1.0f, 1.0f, 1.0f, ColorGradient);

				for (int32 y = TopLeftW.Y; y < BottomRightW.Y; y += EditorSpatialHash->CellSize)
				{
					for (int32 x = TopLeftW.X; x < BottomRightW.X; x += EditorSpatialHash->CellSize)
					{
						const FString CoordText(FString::Printf(TEXT("(%d,%d)"), x / EditorSpatialHash->CellSize, y / EditorSpatialHash->CellSize));
						const FVector2D CoordTextSize = FontMeasure->Measure(CoordText, CoordsFont);

						FSlateDrawElement::MakeText(
							OutDrawElements,
							++LayerId,
							AllottedGeometry.ToPaintGeometry(WorldToScreen.TransformPoint(FVector2D(x + EditorSpatialHash->CellSize / 2, y + EditorSpatialHash->CellSize / 2)) - CoordTextSize / 2, FVector2D(1,1)),
							FString::Printf(TEXT("(%d,%d)"), x / EditorSpatialHash->CellSize, y / EditorSpatialHash->CellSize),
							CoordsFont,
							ESlateDrawEffect::None,
							CoordTextColor
						);
					}
				}
			}
		}

		++LayerId;
	}

	return SWorldPartitionEditorGrid2D::PaintGrid(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId);
}

#undef LOCTEXT_NAMESPACE