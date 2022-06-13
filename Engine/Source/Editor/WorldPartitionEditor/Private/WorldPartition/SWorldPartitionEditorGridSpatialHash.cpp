// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/SWorldPartitionEditorGridSpatialHash.h"
#include "GameFramework/WorldSettings.h"
#include "DerivedDataCache/Public/DerivedDataCacheInterface.h"
#include "Misc/SecureHash.h"
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
#include "Styling/AppStyle.h"
#include "Selection.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DDynamic.h"
#include "Misc/HashBuilder.h"
#include "Fonts/FontMeasure.h"
#include "EngineModule.h"
#include "Renderer/Private/RendererModule.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

static inline FBox2D ToBox2D(const FBox& Box)
{
	return FBox2D(FVector2D(Box.Min), FVector2D(Box.Max));
}

SWorldPartitionEditorGridSpatialHash::SWorldPartitionEditorGridSpatialHash()
	: WorldMiniMapBounds(ForceInit)
{}

SWorldPartitionEditorGridSpatialHash::~SWorldPartitionEditorGridSpatialHash()
{}

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
	if (AWorldPartitionMiniMap* WorldMiniMap = FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(World))
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
	UWorldPartitionEditorSpatialHash* EditorSpatialHash = (UWorldPartitionEditorSpatialHash*)WorldPartition->EditorHash;
	
	// Found the best cell size depending on the current zoom
	int64 EffectiveCellSize = EditorSpatialHash->CellSize;
	for (const UWorldPartitionEditorSpatialHash::FCellNodeHashLevel& HashLevel : EditorSpatialHash->HashLevels)
	{
		const FVector2D CellScreenSize = WorldToScreen.TransformVector(FVector2D(EffectiveCellSize, EffectiveCellSize));
		if (CellScreenSize.X > 32)
		{
			break;
		}

		EffectiveCellSize *= 2;
	}
	
	// Compute visible rect
	const FBox2D ViewRect(FVector2D(ForceInitToZero), AllottedGeometry.GetLocalSize());
	const FBox2D ViewRectWorld(ScreenToWorld.TransformPoint(ViewRect.Min), ScreenToWorld.TransformPoint(ViewRect.Max));

	FBox VisibleGridRectWorld(
		FVector(
			FMath::Max(FMath::FloorToFloat(EditorSpatialHash->Bounds.Min.X / EffectiveCellSize) * EffectiveCellSize, ViewRectWorld.Min.X),
			FMath::Max(FMath::FloorToFloat(EditorSpatialHash->Bounds.Min.Y / EffectiveCellSize) * EffectiveCellSize, ViewRectWorld.Min.Y),
			FMath::FloorToFloat(EditorSpatialHash->Bounds.Min.Z / EffectiveCellSize) * EffectiveCellSize
		),
		FVector(
			FMath::Min(FMath::CeilToFloat(EditorSpatialHash->Bounds.Max.X / EffectiveCellSize) * EffectiveCellSize, ViewRectWorld.Max.X),
			FMath::Min(FMath::CeilToFloat(EditorSpatialHash->Bounds.Max.Y / EffectiveCellSize) * EffectiveCellSize, ViewRectWorld.Max.Y),
			FMath::CeilToFloat(EditorSpatialHash->Bounds.Max.Z / EffectiveCellSize) * EffectiveCellSize
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

			FBox2D UVRegion = WorldMiniMapBrush.GetUVRegion();
			const FVector2D UV0 = UVRegion.Min;
			const FVector2D UV1 = UVRegion.Max;

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

	if (ToBox2D(VisibleGridRectWorld).GetArea() > 0.0f)
	{
		const FLinearColor Color = FLinearColor(0.1f, 0.1f, 0.1f, 1.f);

		UE::Math::TIntVector2<int64> TopLeftW(
			FMath::FloorToFloat(VisibleGridRectWorld.Min.X / EffectiveCellSize) * EffectiveCellSize,
			FMath::FloorToFloat(VisibleGridRectWorld.Min.Y / EffectiveCellSize) * EffectiveCellSize
		);

		UE::Math::TIntVector2<int64> BottomRightW(
			FMath::CeilToFloat(VisibleGridRectWorld.Max.X / EffectiveCellSize) * EffectiveCellSize,
			FMath::CeilToFloat(VisibleGridRectWorld.Max.Y / EffectiveCellSize) * EffectiveCellSize
		);

		TArray<FVector2D> LinePoints;
		LinePoints.SetNum(2);

		// Horizontal
		for (int64 i=TopLeftW.Y; i<=BottomRightW.Y; i+=EffectiveCellSize)
		{
			FVector2D LineStartH(TopLeftW.X, i);
			FVector2D LineEndH(BottomRightW.X, i);

			LinePoints[0] = WorldToScreen.TransformPoint(LineStartH);
			LinePoints[1] = WorldToScreen.TransformPoint(LineEndH);

			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::NoBlending, Color, false, 1.0f);
		}

		// Vertical
		for (int64 i=TopLeftW.X; i<=BottomRightW.X; i+=EffectiveCellSize)
		{
			FVector2D LineStartH(i, TopLeftW.Y);
			FVector2D LineEndH(i, BottomRightW.Y);

			LinePoints[0] = WorldToScreen.TransformPoint(LineStartH);
			LinePoints[1] = WorldToScreen.TransformPoint(LineEndH);

			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::NoBlending, Color, false, 1.0f);
		}

		// Draw coordinates
		if ((EffectiveCellSize == EditorSpatialHash->CellSize) && GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetShowCellCoords())
		{
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FVector2D CellScreenSize = WorldToScreen.TransformVector(FVector2D(EffectiveCellSize, EffectiveCellSize));

			FSlateFontInfo CoordsFont;
			FVector2D DefaultCoordTextSize;
			bool bNeedsGradient = true;
			// Use top-left coordinate as default coord text
			const FString DefaultCoordText(FString::Printf(TEXT("(%lld,%lld)"), TopLeftW.X / EffectiveCellSize, TopLeftW.Y / EffectiveCellSize));
			for(int32 DesiredFontSize = 24; DesiredFontSize >= 8; DesiredFontSize -= 2)
			{
				CoordsFont = FCoreStyle::GetDefaultFontStyle("Bold", DesiredFontSize);
				DefaultCoordTextSize = FontMeasure->Measure(DefaultCoordText, CoordsFont);

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

				for (int64 y = TopLeftW.Y; y < BottomRightW.Y; y += EffectiveCellSize)
				{
					for (int64 x = TopLeftW.X; x < BottomRightW.X; x += EffectiveCellSize)
					{
						const FString CoordText(FString::Printf(TEXT("(%lld,%lld)"), x / EffectiveCellSize, y / EffectiveCellSize));
						const FVector2D CoordTextSize = FontMeasure->Measure(CoordText, CoordsFont);

						FSlateDrawElement::MakeText(
							OutDrawElements,
							++LayerId,
							AllottedGeometry.ToPaintGeometry(WorldToScreen.TransformPoint(FVector2D(x + EffectiveCellSize / 2, y + EffectiveCellSize / 2)) - CoordTextSize / 2, FVector2D(1,1)),
							FString::Printf(TEXT("(%lld,%lld)"), x / EffectiveCellSize, y / EffectiveCellSize),
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