// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementPlaceSingleTool.h"
#include "UObject/Object.h"
#include "AssetPlacementSettings.h"
#include "Subsystems/PlacementSubsystem.h"
#include "ScopedTransaction.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"

constexpr TCHAR UPlacementModePlaceSingleTool::ToolName[];

UPlacementBrushToolBase* UPlacementModePlaceSingleToolBuilder::FactoryToolInstance(UObject* Outer) const
{
	return NewObject<UPlacementModePlaceSingleTool>(Outer);
}

void UPlacementModePlaceSingleTool::OnEndDrag(const FRay& Ray)
{
	Super::OnEndDrag(Ray);

	if (PlacementSettings.IsValid() && PlacementSettings->PaletteItems.Num())
	{
		int32 ItemIndex = FMath::RandHelper(PlacementSettings->PaletteItems.Num());
		const FPaletteItem& ItemToPlace = PlacementSettings->PaletteItems[ItemIndex];
		if (ItemToPlace.AssetData.IsValid())
		{
			FAssetPlacementInfo PlacementInfo;
			PlacementInfo.AssetToPlace = ItemToPlace.AssetData;
			PlacementInfo.FactoryOverride = ItemToPlace.FactoryOverride;
			PlacementInfo.FinalizedTransform = FTransform(LastBrushStamp.HitResult.ImpactPoint);
			PlacementInfo.PreferredLevel = GEditor->GetEditorWorldContext().World()->GetCurrentLevel();

			FPlacementOptions PlacementOptions;
			PlacementOptions.bPreferBatchPlacement = true;
			PlacementOptions.bPreferInstancedPlacement = true;

			UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>();
			if (PlacementSubsystem)
			{
				FScopedTransaction Transaction(NSLOCTEXT("PlacementMode", "SinglePlaceAsset", "Place Single Asset"));
				PlacementSubsystem->PlaceAsset(PlacementInfo, PlacementOptions);
			}
		}
	}
}
