// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementPlaceSingleTool.h"
#include "UObject/Object.h"
#include "AssetPlacementSettings.h"
#include "Subsystems/PlacementSubsystem.h"
#include "ScopedTransaction.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "Modes/PlacementModeSubsystem.h"

constexpr TCHAR UPlacementModePlaceSingleTool::ToolName[];

UPlacementBrushToolBase* UPlacementModePlaceSingleToolBuilder::FactoryToolInstance(UObject* Outer) const
{
	return NewObject<UPlacementModePlaceSingleTool>(Outer);
}

bool UPlacementModePlaceSingleSettings::CanEditChange(const FProperty* Property) const
{
	if (!Super::CanEditChange(Property))
	{
		return false;
	}

	const FName PropertyName = Property->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UPlacementModePlaceSingleSettings, bInvertCursorAxis))
	{
		return bAlignToCursor;
	}

	return true;
}

void UPlacementModePlaceSingleTool::Setup()
{
	Super::Setup();

	SingleToolSettings = NewObject<UPlacementModePlaceSingleSettings>(this);
	SingleToolSettings->LoadConfig();
	AddToolPropertySource(SingleToolSettings);
}

void UPlacementModePlaceSingleTool::Shutdown(EToolShutdownType ShutdownType)
{
	Super::Shutdown(ShutdownType);

	SingleToolSettings->SaveConfig();
	RemoveToolPropertySource(SingleToolSettings);
	SingleToolSettings = nullptr;
}

void UPlacementModePlaceSingleTool::OnEndDrag(const FRay& Ray)
{
	Super::OnEndDrag(Ray);

	TWeakObjectPtr<const UAssetPlacementSettings> PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	if (PlacementSettings.IsValid() && PlacementSettings->PaletteItems.Num())
	{
		int32 ItemIndex = FMath::RandHelper(PlacementSettings->PaletteItems.Num());
		const FPaletteItem& ItemToPlace = PlacementSettings->PaletteItems[ItemIndex];
		if (ItemToPlace.AssetData.IsValid())
		{
			FAssetPlacementInfo PlacementInfo;
			PlacementInfo.AssetToPlace = ItemToPlace.AssetData;
			PlacementInfo.FactoryOverride = ItemToPlace.FactoryOverride;
			PlacementInfo.FinalizedTransform = GetFinalTransformFromHitLocationAndNormal(LastBrushStamp.HitResult.ImpactPoint, LastBrushStamp.HitResult.ImpactNormal);
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

FRotator UPlacementModePlaceSingleTool::GetFinalRotation(const FTransform& InTransform)
{
	return InTransform.Rotator();
}
