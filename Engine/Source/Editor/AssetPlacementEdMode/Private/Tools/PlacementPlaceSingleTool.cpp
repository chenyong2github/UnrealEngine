// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementPlaceSingleTool.h"
#include "UObject/Object.h"
#include "AssetPlacementSettings.h"
#include "Subsystems/PlacementSubsystem.h"
#include "ScopedTransaction.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "Modes/PlacementModeSubsystem.h"
#include "InteractiveToolManager.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"

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

UPlacementModePlaceSingleTool::UPlacementModePlaceSingleTool() = default;
UPlacementModePlaceSingleTool::~UPlacementModePlaceSingleTool() = default;
UPlacementModePlaceSingleTool::UPlacementModePlaceSingleTool(FVTableHelper& Helper)
	: Super(Helper)
{
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
	DestroyPreviewElements();

	SingleToolSettings->SaveConfig();
	RemoveToolPropertySource(SingleToolSettings);
	SingleToolSettings = nullptr;

	Super::Shutdown(ShutdownType);
}

void UPlacementModePlaceSingleTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	Super::OnClickPress(PressPos);

	if (bCtrlToggle)
	{
		// If this was a ctrl click, regenerate the preview placement information, and destroy any active gizmos.
		GeneratePreviewPlacementData(PressPos);
	}
	else
	{
		// Otherwise, and we're not currently editing something that was placed, then place the preview data.
		// Todo - verify we don't get here if the gizmos are active
		FPlacementOptions PlacementOptions;
		PlacementOptions.bPreferBatchPlacement = true;
		PlacementOptions.bPreferInstancedPlacement = true;

		UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>();
		if (PlacementSubsystem)
		{
			// Update the level, just in case the preview moved us out of the current one.
			PreviewPlacementInfo->PreferredLevel = GEditor->GetEditorWorldContext().World()->GetCurrentLevel();

			FScopedTransaction Transaction(NSLOCTEXT("PlacementMode", "SinglePlaceAsset", "Place Single Asset"));
			PlacementSubsystem->PlaceAsset(*PreviewPlacementInfo, PlacementOptions);
		}

		DestroyPreviewElements();
	}
}

void UPlacementModePlaceSingleTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	Super::OnBeginHover(DevicePos);

	// Always regenerate the placement data when a hover sequence begins
	GeneratePreviewPlacementData(DevicePos);
}

bool UPlacementModePlaceSingleTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (!Super::OnUpdateHover(DevicePos))
	{
		return false;
	}

	// Update the preview elements
	UpdatePreviewElements(DevicePos);
	return true;
}

void UPlacementModePlaceSingleTool::OnEndHover()
{
	Super::OnEndHover();

	// remove the preview elements from the level
	DestroyPreviewElements();
}

void UPlacementModePlaceSingleTool::GeneratePreviewPlacementData(const FInputDeviceRay& DevicePos)
{
	PreviewPlacementInfo.Reset();

	TWeakObjectPtr<const UAssetPlacementSettings> PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	if (PlacementSettings.IsValid() && PlacementSettings->PaletteItems.Num())
	{
		int32 ItemIndex = FMath::RandHelper(PlacementSettings->PaletteItems.Num());
		const FPaletteItem& ItemToPlace = PlacementSettings->PaletteItems[ItemIndex];

		PreviewPlacementInfo = MakeUnique<FAssetPlacementInfo>();
		PreviewPlacementInfo->AssetToPlace = ItemToPlace.AssetData;
		PreviewPlacementInfo->FactoryOverride = ItemToPlace.FactoryOverride;
		PreviewPlacementInfo->FinalizedTransform = GetFinalTransformFromHitLocationAndNormal(LastBrushStamp.HitResult.ImpactPoint, LastBrushStamp.HitResult.ImpactNormal);
		PreviewPlacementInfo->PreferredLevel = GEditor->GetEditorWorldContext().World()->GetCurrentLevel();
	}
}

void UPlacementModePlaceSingleTool::CreatePreviewElements(const FInputDeviceRay& DevicePos)
{
	// Place the preview elements from our stored info
	if (!PreviewPlacementInfo.IsValid())
	{
		GeneratePreviewPlacementData(DevicePos);
	}

	if (UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
	{
		FPlacementOptions PlacementOptions;
		PlacementOptions.bIsCreatingPreviewElements = true;

		PreviewElements = PlacementSubsystem->PlaceAsset(*PreviewPlacementInfo, PlacementOptions);
	}
	
	// Disable collision on any preview elements
	for (FTypedElementHandle& PreviewElement : PreviewElements)
	{
		if (TTypedElement<UTypedElementWorldInterface> WorldInterfaceElement = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(PreviewElement))
		{
			WorldInterfaceElement.NotifyMovementStarted();
		}
	}
}

void UPlacementModePlaceSingleTool::UpdatePreviewElements(const FInputDeviceRay& DevicePos)
{
	if (PreviewElements.Num() == 0)
	{
		CreatePreviewElements(DevicePos);
	}

	if (PreviewElements.Num() == 0)
	{
		return;
	}

	// Update the location.
	FTransform& FinalizedTransform = PreviewPlacementInfo->FinalizedTransform;
	FinalizedTransform.SetLocation(LastBrushStamp.HitResult.ImpactPoint);

	// Update the rotation.
	TWeakObjectPtr<const UAssetPlacementSettings> PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	if (PlacementSettings.IsValid() && PlacementSettings->bAlignToNormal)
	{
		FQuat UpdatedRotation = UpdateRotationAlignedToBrushNormal(PlacementSettings->AxisToAlignWithNormal, PlacementSettings->bInvertNormalAxis);
		FinalizedTransform.SetRotation(UpdatedRotation);
	}

	// Update the transform positions for the preview elements.
	for (FTypedElementHandle& PreviewElement : PreviewElements)
	{
		if (TTypedElement<UTypedElementWorldInterface> WorldInterfaceElement = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(PreviewElement))
		{
			WorldInterfaceElement.SetWorldTransform(FinalizedTransform);
			WorldInterfaceElement.NotifyMovementOngoing();
		}
	}
}

void UPlacementModePlaceSingleTool::DestroyPreviewElements()
{
	FToolBuilderState SelectionState;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);
	check(SelectionState.TypedElementSelectionSet.IsValid());	// Placement tools expect a valid selection set.

	for (FTypedElementHandle& PreviewElement : PreviewElements)
	{
		if (TTypedElement<UTypedElementWorldInterface> WorldInterfaceElement = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(PreviewElement))
		{
			WorldInterfaceElement.NotifyMovementEnded();
			WorldInterfaceElement.DeleteElement(WorldInterfaceElement.GetOwnerWorld(), SelectionState.TypedElementSelectionSet.Get(), FTypedElementDeletionOptions());
		}
	}

	PreviewElements.Empty();
}
