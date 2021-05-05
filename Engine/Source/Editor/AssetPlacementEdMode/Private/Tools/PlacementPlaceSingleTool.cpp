// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementPlaceSingleTool.h"

#include "AssetPlacementSettings.h"
#include "Editor.h"
#include "InteractiveToolManager.h"
#include "ScopedTransaction.h"
#include "ToolDataVisualizer.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "Editor/EditorEngine.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Elements/SMInstance/SMInstanceElementData.h" // For SMInstanceElementDataUtil::SMInstanceElementsEnabled
#include "Modes/PlacementModeSubsystem.h"
#include "Subsystems/PlacementSubsystem.h"
#include "UObject/Object.h"
#include "Tools/AssetEditorContextInterface.h"
#include "EditorModeManager.h"
#include "Toolkits/IToolkitHost.h"
#include "ContextObjectStore.h"

constexpr TCHAR UPlacementModePlaceSingleTool::ToolName[];

UPlacementBrushToolBase* UPlacementModePlaceSingleToolBuilder::FactoryToolInstance(UObject* Outer) const
{
	return NewObject<UPlacementModePlaceSingleTool>(Outer);
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

	SetupRightClickMouseBehavior();

	bIsTweaking = false;

	SinglePlaceSettings = NewObject<UPlacementModePlaceSingleToolSettings>(this);
	SinglePlaceSettings->LoadConfig();
	AddToolPropertySource(SinglePlaceSettings);
}

void UPlacementModePlaceSingleTool::Shutdown(EToolShutdownType ShutdownType)
{
	DestroyPreviewElements();

	// Preserve the selection on exiting the tool, so that a users' state persists as they use the mode.
	constexpr bool bClearSelectionSet = false;
	ExitTweakState(bClearSelectionSet);

	Super::Shutdown(ShutdownType);

	SinglePlaceSettings->SaveConfig();
	SinglePlaceSettings = nullptr;
}

void UPlacementModePlaceSingleTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	Super::OnClickPress(PressPos);

	// Place the Preview data if we managed to get to a valid handled click.
	FPlacementOptions PlacementOptions;
	PlacementOptions.bPreferBatchPlacement = true;
	PlacementOptions.bPreferInstancedPlacement = SMInstanceElementDataUtil::SMInstanceElementsEnabled();
	
	UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>();
	if (PlacementSubsystem)
	{
		FAssetPlacementInfo FinalizedPlacementInfo = *PlacementInfo;
		FinalizedPlacementInfo.FinalizedTransform = FinalizeTransform(
			FTransform(PlacementInfo->FinalizedTransform.GetRotation(), LastBrushStamp.WorldPosition, PlacementInfo->FinalizedTransform.GetScale3D()),
			LastBrushStamp.WorldNormal,
			GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject());
		FinalizedPlacementInfo.PreferredLevel = GEditor->GetEditorWorldContext().World()->GetCurrentLevel();

		GetToolManager()->BeginUndoTransaction(NSLOCTEXT("PlacementMode", "SinglePlaceAsset", "Place Single Asset"));
		PlacedElements = PlacementSubsystem->PlaceAsset(FinalizedPlacementInfo, PlacementOptions);
		NotifyMovementStarted(PlacedElements);
	}
}

void UPlacementModePlaceSingleTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	FVector TraceStartLocation = DragPos.WorldRay.Origin;
	FVector TraceDirection = DragPos.WorldRay.Direction;
	FVector TraceEndLocation = TraceStartLocation + (TraceDirection * HALF_WORLD_MAX);
	FVector TraceIntersectionXY = FMath::LinePlaneIntersection(TraceStartLocation, TraceEndLocation, FPlane(LastBrushStamp.WorldPosition, LastBrushStamp.WorldNormal));

	FVector CursorDirection;
	float CursorDistance;
	FVector MouseDelta = LastBrushStamp.WorldPosition - TraceIntersectionXY;
	MouseDelta.ToDirectionAndLength(CursorDirection, CursorDistance);
	if (CursorDirection.IsNearlyZero())
	{
		return;
	}

	const UAssetPlacementSettings* PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();

	// Update rotation based on mouse position
	FTransform UpdatedTransform = FinalizeTransform(
		FTransform(FRotationMatrix::MakeFromXZ(CursorDirection, PlacementInfo->FinalizedTransform.GetRotation().GetUpVector()).ToQuat(), LastBrushStamp.WorldPosition, PlacementInfo->FinalizedTransform.GetScale3D()),
		LastBrushStamp.WorldNormal,
		PlacementSettings);

	// Update scale based on mouse position
	FVector UpdatedScale = UpdatedTransform.GetScale3D();
	if (PlacementSettings && (SinglePlaceSettings->ScalingType != EPlacementScaleToCursorType::None))
	{
		auto UpdateComponent = [PlacementSettings, CursorDistance, this](float InComponent) -> float
		{
			float Sign = 1.0f;
			if (InComponent < 0.0f)
			{
				Sign = -1.0f;
			}

			FFloatInterval ScaleRange(FMath::Abs(InComponent), PlacementSettings->ScaleRange.Max);
			float NewComponent = ScaleRange.Interpolate(FMath::Min(1.0f, (CursorDistance / BrushStampIndicator->BrushRadius)));
			return NewComponent * Sign;
		};

		switch (PlacementSettings->ScalingType)
		{
			case EFoliageScaling::LockXY:
			{
				UpdatedScale.Z = UpdateComponent(UpdatedScale.Z);
				break;
			}
			case EFoliageScaling::LockYZ:
			{
				UpdatedScale.X = UpdateComponent(UpdatedScale.X);
				break;
			}
			case EFoliageScaling::LockXZ:
			{
				UpdatedScale.Y = UpdateComponent(UpdatedScale.Y);
				break;
			}
			default:
			{
				UpdatedScale.X = UpdateComponent(UpdatedScale.X);
				UpdatedScale.Y = UpdateComponent(UpdatedScale.Y);
				UpdatedScale.Z = UpdateComponent(UpdatedScale.Z);
				break;
			}
		}
	}
	UpdatedTransform.SetScale3D(UpdatedScale);

	// Use the drag position and settings to update the scale and rotation of the placed elements.
	UpdateElementTransforms(PlacedElements, UpdatedTransform, false);
}

void UPlacementModePlaceSingleTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	Super::OnClickRelease(ReleasePos);

	NotifyMovementEnded(PlacedElements);
	EnterTweakState(PlacedElements);
	GetToolManager()->EndUndoTransaction();

	ShutdownBrushStampIndicator();
	PlacementInfo.Reset();
}

void UPlacementModePlaceSingleTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	Super::OnBeginHover(DevicePos);

	// Always regenerate the placement data when a hover sequence begins
	PlacementInfo.Reset();
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

void UPlacementModePlaceSingleTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	// Transform the brush radius to standard pixel size
	float BrushRadiusScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(RenderAPI->GetSceneView(), LastBrushStamp.WorldPosition);
	LastBrushStamp.Radius = 100.0f * BrushRadiusScale;

	Super::Render(RenderAPI);
}

void UPlacementModePlaceSingleTool::GeneratePlacementData(const FInputDeviceRay& DevicePos)
{
	const UAssetPlacementSettings* PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	if (PlacementSettings && PlacementSettings->PaletteItems.Num())
	{
		int32 ItemIndex = FMath::RandHelper(PlacementSettings->PaletteItems.Num());
		const TSharedPtr<FPaletteItem>& ItemToPlace = PlacementSettings->PaletteItems[ItemIndex];
		if (!ItemToPlace)
		{
			return;
		}

		FTransform TransformToUpdate(GenerateRandomRotation(PlacementSettings), LastBrushStamp.WorldPosition, GenerateRandomScale(PlacementSettings));

		PlacementInfo = MakeUnique<FAssetPlacementInfo>();
		PlacementInfo->AssetToPlace = ItemToPlace->AssetData;
		PlacementInfo->FactoryOverride = ItemToPlace->AssetFactoryInterface;
		PlacementInfo->FinalizedTransform = FinalizeTransform(TransformToUpdate, LastBrushStamp.WorldNormal, PlacementSettings);
		PlacementInfo->PreferredLevel = GEditor->GetEditorWorldContext().World()->GetCurrentLevel();
	}
}

void UPlacementModePlaceSingleTool::CreatePreviewElements(const FInputDeviceRay& DevicePos)
{
	SetupBrushStampIndicator();

	// Place the preview elements from our stored info
	if (!PlacementInfo)
	{
		GeneratePlacementData(DevicePos);
	}

	if (UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
	{
		FPlacementOptions PlacementOptions;
		PlacementOptions.bIsCreatingPreviewElements = true;
		FAssetPlacementInfo InfoToPlace = *PlacementInfo;
		InfoToPlace.FinalizedTransform = FinalizeTransform(
			FTransform(PlacementInfo->FinalizedTransform.GetRotation(), LastBrushStamp.WorldPosition, PlacementInfo->FinalizedTransform.GetScale3D()),
			LastBrushStamp.WorldNormal,
			GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject());

		PreviewElements = PlacementSubsystem->PlaceAsset(InfoToPlace, PlacementOptions);
	}

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

	FTransform UpdatedTransform(PlacementInfo->FinalizedTransform.GetRotation(), LastBrushStamp.WorldPosition, PlacementInfo->FinalizedTransform.GetScale3D());
	UpdatedTransform = FinalizeTransform(UpdatedTransform, LastBrushStamp.WorldNormal, GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject());

	UpdateElementTransforms(PreviewElements, UpdatedTransform);
}

void UPlacementModePlaceSingleTool::DestroyPreviewElements()
{
	NotifyMovementEnded(PreviewElements);
	PreviewElements.Empty();
}

void UPlacementModePlaceSingleTool::EnterTweakState(TArrayView<const FTypedElementHandle> InElementHandles)
{
	if (InElementHandles.Num() == 0 || !SinglePlaceSettings->bSelectAfterPlacing)
	{
		return;
	}

	if (IAssetEditorContextInterface* AssetEditorContext = GetToolManager()->GetContextObjectStore()->FindContext<IAssetEditorContextInterface>())
	{
		if (UTypedElementSelectionSet* SelectionSet = AssetEditorContext->GetMutableSelectionSet())
		{
			SelectionSet->SetSelection(InElementHandles, FTypedElementSelectionOptions());
			bIsTweaking = true;
		}
	}
}

void UPlacementModePlaceSingleTool::ExitTweakState(bool bClearSelectionSet)
{
	if (bClearSelectionSet)
	{
		if (IAssetEditorContextInterface* AssetEditorContext = GetToolManager()->GetContextObjectStore()->FindContext<IAssetEditorContextInterface>())
		{
			if (UTypedElementSelectionSet* SelectionSet = AssetEditorContext->GetMutableSelectionSet())
			{
				SelectionSet->ClearSelection(FTypedElementSelectionOptions());
			}
		}
	}

	bIsTweaking = false;
	PlacedElements.Empty();
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
	IAssetEditorContextInterface* AssetEditorContext = GetToolManager()->GetContextObjectStore()->FindContext<IAssetEditorContextInterface>();
	if (!AssetEditorContext)
	{
		return;
	}

	UTypedElementSelectionSet* SelectionSet = AssetEditorContext->GetMutableSelectionSet();
	if (!SelectionSet)
	{
		return;
	}

	for (const FTypedElementHandle& PreviewElement : PreviewElements)
	{
		if (TTypedElement<UTypedElementWorldInterface> WorldInterfaceElement = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementWorldInterface>(PreviewElement))
		{
			WorldInterfaceElement.NotifyMovementEnded();
			WorldInterfaceElement.DeleteElement(WorldInterfaceElement.GetOwnerWorld(), SelectionSet, FTypedElementDeletionOptions());
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
		PlacementInfo.Reset();
	};
	RightMouseBehavior->SetDefaultPriority(FInputCapturePriority(-1));
	RightMouseBehavior->SetUseRightMouseButton();
	RightMouseBehavior->Initialize();
	AddInputBehavior(RightMouseBehavior);
}
