// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementPlaceSingleTool.h"

#include "AssetPlacementSettings.h"
#include "Editor.h"
#include "InteractiveToolManager.h"
#include "ScopedTransaction.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "Editor/EditorEngine.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Modes/PlacementModeSubsystem.h"
#include "Subsystems/PlacementSubsystem.h"
#include "UObject/Object.h"

constexpr TCHAR UPlacementModePlaceSingleTool::ToolName[];

UPlacementBrushToolBase* UPlacementModePlaceSingleToolBuilder::FactoryToolInstance(UObject* Outer) const
{
	return NewObject<UPlacementModePlaceSingleTool>(Outer);
}

UPlacementModePlaceSingleTool::UPlacementModePlaceSingleTool() = default;
UPlacementModePlaceSingleTool::~UPlacementModePlaceSingleTool() = default;
UPlacementModePlaceSingleTool::UPlacementModePlaceSingleTool(FVTableHelper& Helper)
	: Super(Helper)
	, LastGeneratedRotation(FQuat::Identity)
{
}

void UPlacementModePlaceSingleTool::Setup()
{
	Super::Setup();

	SetupRightClickMouseBehavior();

	bIsTweaking = false;
}

void UPlacementModePlaceSingleTool::Shutdown(EToolShutdownType ShutdownType)
{
	DestroyPreviewElements();

	// Preserve the selection on exiting the tool, so that a users' state persists as they use the mode.
	constexpr bool bClearSelectionSet = false;
	ExitTweakState(bClearSelectionSet);

	Super::Shutdown(ShutdownType);
}

void UPlacementModePlaceSingleTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	Super::OnClickPress(PressPos);

	// Update the preview element one final time to be sure the transform is updated.
	UpdatePreviewElements(PressPos);
	DestroyPreviewElements();

	// Place the Preview data if we managed to get to a valid handled click.
	FPlacementOptions PlacementOptions;
	PlacementOptions.bPreferBatchPlacement = true;
	PlacementOptions.bPreferInstancedPlacement = false;	// Off until ISM single selection support added.

	UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>();
	if (PlacementSubsystem)
	{
		// Update the level, just in case the preview moved us out of the current one.
		PreviewPlacementInfo->PreferredLevel = GEditor->GetEditorWorldContext().World()->GetCurrentLevel();

		FScopedTransaction Transaction(NSLOCTEXT("PlacementMode", "SinglePlaceAsset", "Place Single Asset"));
		EnterTweakState(PlacementSubsystem->PlaceAsset(*PreviewPlacementInfo, PlacementOptions));
	}
}

void UPlacementModePlaceSingleTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	Super::OnBeginHover(DevicePos);

	// Always regenerate the placement data when a hover sequence begins
	CreatePreviewElements(DevicePos);
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

	// Destroy the preview elements we created.
	DestroyPreviewElements();
}

FInputRayHit UPlacementModePlaceSingleTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	if (!bIsTweaking)
	{
		return Super::CanBeginClickDragSequence(PressPos);
	}
	
	return FInputRayHit();
}

FInputRayHit UPlacementModePlaceSingleTool::BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos)
{
	if (!bIsTweaking)
	{
		return Super::BeginHoverSequenceHitTest(DevicePos);
	}

	return FInputRayHit();
}

void UPlacementModePlaceSingleTool::GeneratePreviewPlacementData(const FInputDeviceRay& DevicePos)
{
	PreviewPlacementInfo.Reset();
	LastGeneratedRotation = FQuat::Identity;

	const UAssetPlacementSettings* PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	if (PlacementSettings && PlacementSettings->PaletteItems.Num())
	{
		int32 ItemIndex = FMath::RandHelper(PlacementSettings->PaletteItems.Num());
		const FPaletteItem& ItemToPlace = PlacementSettings->PaletteItems[ItemIndex];
		LastGeneratedRotation = GenerateRandomRotation(PlacementSettings);
		FTransform TransformToUpdate(LastGeneratedRotation, LastBrushStamp.WorldPosition, GenerateRandomScale(PlacementSettings));

		PreviewPlacementInfo = MakeUnique<FAssetPlacementInfo>();
		PreviewPlacementInfo->AssetToPlace = ItemToPlace.AssetData;
		PreviewPlacementInfo->FactoryOverride = ItemToPlace.FactoryOverride;
		PreviewPlacementInfo->FinalizedTransform = FinalizeTransform(TransformToUpdate, LastBrushStamp.WorldNormal, PlacementSettings);
		PreviewPlacementInfo->PreferredLevel = GEditor->GetEditorWorldContext().World()->GetCurrentLevel();
	}
}

void UPlacementModePlaceSingleTool::CreatePreviewElements(const FInputDeviceRay& DevicePos)
{
	// Place the preview elements from our stored info
	if (!PreviewPlacementInfo)
	{
		GeneratePreviewPlacementData(DevicePos);
	}

	if (UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
	{
		FPlacementOptions PlacementOptions;
		PlacementOptions.bIsCreatingPreviewElements = true;

		PreviewElements = PlacementSubsystem->PlaceAsset(*PreviewPlacementInfo, PlacementOptions);
	}

	SetupBrushStampIndicator();

	NotifyMovementStarted(PreviewElements);
}

void UPlacementModePlaceSingleTool::UpdatePreviewElements(const FInputDeviceRay& DevicePos)
{
	// If we should have preview elements, but do not currently, go ahead and create them.
	if ((PreviewElements.Num() == 0) && !BrushStampIndicator)
	{
		CreatePreviewElements(DevicePos);
	}

	// If we don't actually have any preview handles created, we don't need to update them, so go ahead and bail.
	if (PreviewElements.Num() == 0)
	{
		return;
	}

	FTransform UpdatedTransform(LastGeneratedRotation, LastBrushStamp.WorldPosition, PreviewPlacementInfo->FinalizedTransform.GetScale3D());
	UpdatedTransform = FinalizeTransform(UpdatedTransform, LastBrushStamp.WorldNormal, GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject());

	UpdateElementTransforms(PreviewElements, UpdatedTransform);
	PreviewPlacementInfo->FinalizedTransform = UpdatedTransform;
}

void UPlacementModePlaceSingleTool::DestroyPreviewElements()
{
	NotifyMovementEnded(PreviewElements);
	PreviewElements.Empty();
	ShutdownBrushStampIndicator();
}

void UPlacementModePlaceSingleTool::EnterTweakState(TArrayView<const FTypedElementHandle> InElementHandles)
{
	if (InElementHandles.Num() == 0)
	{
		return;
	}

	FToolBuilderState SelectionState;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);
	SelectionState.TypedElementSelectionSet->SetSelection(InElementHandles, FTypedElementSelectionOptions());
	bIsTweaking = true;
}

void UPlacementModePlaceSingleTool::ExitTweakState(bool bClearSelectionSet)
{
	if (bClearSelectionSet)
	{
		FToolBuilderState SelectionState;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);
		SelectionState.TypedElementSelectionSet->ClearSelection(FTypedElementSelectionOptions());
	}

	bIsTweaking = false;
}

void UPlacementModePlaceSingleTool::UpdateElementTransforms(TArrayView<const FTypedElementHandle> InElements, const FTransform& InTransform, bool bLocalTransform)
{
	// Update the transform positions for the preview elements.
	for (const FTypedElementHandle& ElementHandle : InElements)
	{
		if (TTypedElement<UTypedElementWorldInterface> WorldInterfaceElement = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(ElementHandle))
		{
			bLocalTransform ? WorldInterfaceElement.SetRelativeTransform(InTransform) : WorldInterfaceElement.SetWorldTransform(InTransform);
			WorldInterfaceElement.NotifyMovementOngoing();
		}
	}
}

void UPlacementModePlaceSingleTool::NotifyMovementStarted(TArrayView<const FTypedElementHandle> InElements)
{
	// Notify Movement started
	for (const FTypedElementHandle& PreviewElement : PreviewElements)
	{
		if (TTypedElement<UTypedElementWorldInterface> WorldInterfaceElement = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(PreviewElement))
		{
			WorldInterfaceElement.NotifyMovementStarted();
		}
	}
}

void UPlacementModePlaceSingleTool::NotifyMovementEnded(TArrayView<const FTypedElementHandle> InElements)
{
	FToolBuilderState SelectionState;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);
	check(SelectionState.TypedElementSelectionSet.IsValid());	// Placement tools expect a valid selection set.

	for (const FTypedElementHandle& PreviewElement : PreviewElements)
	{
		if (TTypedElement<UTypedElementWorldInterface> WorldInterfaceElement = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(PreviewElement))
		{
			WorldInterfaceElement.NotifyMovementEnded();
			WorldInterfaceElement.DeleteElement(WorldInterfaceElement.GetOwnerWorld(), SelectionState.TypedElementSelectionSet.Get(), FTypedElementDeletionOptions());
		}
	}
}

void UPlacementModePlaceSingleTool::SetupRightClickMouseBehavior()
{
	ULocalClickDragInputBehavior* RightMouseBehavior = NewObject<ULocalClickDragInputBehavior>(this);
	RightMouseBehavior->CanBeginClickDragFunc = [this](const FInputDeviceRay&) { return bShiftToggle ? FInputRayHit(1.0f) : FInputRayHit(); };
	RightMouseBehavior->OnClickReleaseFunc = [this](const FInputDeviceRay&)
	{
		constexpr bool bClearSelection = true;
		ExitTweakState(bClearSelection);
		PreviewPlacementInfo.Reset();
	};
	RightMouseBehavior->SetDefaultPriority(FInputCapturePriority(-1));
	RightMouseBehavior->SetUseRightMouseButton();
	RightMouseBehavior->Initialize();
	AddInputBehavior(RightMouseBehavior);
}
