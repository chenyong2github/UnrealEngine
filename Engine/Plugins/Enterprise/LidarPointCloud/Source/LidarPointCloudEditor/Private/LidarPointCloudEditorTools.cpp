// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudEditorTools.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "LidarPointCloudEditorHelper.h"
#include "LidarPointCloudShared.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"

#define LOCTEXT_NAMESPACE "LidarPointCloudEditorTool"

namespace {
	// Distance Square between the first and last points of the polygonal selection, where the shape will be considered as closed
	constexpr int32 PolySnapDistanceSq = 250;

	// Affects the frequency of new point injections when drawing lasso-based shapes
	constexpr int32 LassoSpacingSq = 250;

	// Affects the max depth delta when painting. Prevents the brush from "falling through" the gaps.
	constexpr float PaintMaxDeviation = 0.15f;
}

void ULidarEditorTool_Base::Setup()
{
	Super::Setup();

	ToolActions = CreateToolActions();

	if(ToolActions)
	{
		AddToolPropertySource(ToolActions);
	}
}

FText ULidarEditorTool_Base::GetToolMessage() const
{
	return FText();
}

void ULidarEditorTool_ClickDragBase::Setup()
{
	Super::Setup();

	HoverBehavior = NewObject<UMouseHoverBehavior>();	
	HoverBehavior->Modifiers.RegisterModifier(1, FInputDeviceState::IsShiftKeyDown);
	HoverBehavior->Modifiers.RegisterModifier(2, FInputDeviceState::IsCtrlKeyDown);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior, this);
	
	ClickDragBehavior = NewObject<UClickDragInputBehavior>();
	ClickDragBehavior->Initialize(this);
	ClickDragBehavior->Modifiers.RegisterModifier(1, FInputDeviceState::IsShiftKeyDown);
	ClickDragBehavior->Modifiers.RegisterModifier(2, FInputDeviceState::IsCtrlKeyDown);
	AddInputBehavior(ClickDragBehavior, this);
}

void ULidarEditorTool_ClickDragBase::Render(IToolsContextRenderAPI* RenderAPI)
{
	Super::Render(RenderAPI);
	
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
}

void ULidarEditorTool_ClickDragBase::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	switch (ModifierID)
	{
	case 1:
		bShiftToggle = bIsOn;
		break;
	case 2:
		bCtrlToggle = bIsOn;
		break;
	default:
		break;
	}
}

void ULidarToolActions_Align::AlignAroundWorldOrigin()
{
	FLidarPointCloudEditorHelper::AlignSelectionAroundWorldOrigin();
}

void ULidarToolActions_Align::AlignAroundOriginalCoordinates()
{
	FLidarPointCloudEditorHelper::SetOriginalCoordinateForSelection();
}

void ULidarToolActions_Align::ResetAlignment()
{
	FLidarPointCloudEditorHelper::CenterSelection();
}

void ULidarToolActions_Merge::MergeActors()
{
	FLidarPointCloudEditorHelper::MergeSelectionByComponent(bReplaceSourceActorsAfterMerging);
}

void ULidarToolActions_Merge::MergeData()
{
	FLidarPointCloudEditorHelper::MergeSelectionByData(bReplaceSourceActorsAfterMerging);
}

void ULidarToolActions_Collision::BuildCollision()
 {
 	FLidarPointCloudEditorHelper::SetCollisionErrorForSelection(OverrideMaxCollisionError);
 	FLidarPointCloudEditorHelper::BuildCollisionForSelection();
 }

void ULidarToolActions_Collision::RemoveCollision()
 {
 	FLidarPointCloudEditorHelper::RemoveCollisionForSelection();
 }

void ULidarToolActions_Meshing::BuildStaticMesh()
{
	FLidarPointCloudEditorHelper::MeshSelected(false, MaxMeshingError, bMergeMeshes, !bMergeMeshes && bRetainTransform);
}

void ULidarToolActions_Normals::CalculateNormals()
{
	FLidarPointCloudEditorHelper::SetNormalsQualityForSelection(Quality, NoiseTolerance);
	FLidarPointCloudEditorHelper::CalculateNormalsForSelection();
}

void ULidarToolActions_Selection::HideSelected()
{
	FLidarPointCloudEditorHelper::HideSelected();
}

void ULidarToolActions_Selection::ResetVisibility()
{
	FLidarPointCloudEditorHelper::ResetVisibility();
}

void ULidarToolActions_Selection::DeleteHidden()
{
	FLidarPointCloudEditorHelper::DeleteHidden();
}

void ULidarToolActions_Selection::Extract()
{
	FLidarPointCloudEditorHelper::Extract();
}

void ULidarToolActions_Selection::ExtractAsCopy()
{
	FLidarPointCloudEditorHelper::ExtractAsCopy();
}

void ULidarToolActions_Selection::CalculateNormals()
{
	FLidarPointCloudEditorHelper::CalculateNormals();
	ClearSelection();
}

void ULidarToolActions_Selection::DeleteSelected()
{
	FLidarPointCloudEditorHelper::DeleteSelected();
}

void ULidarToolActions_Selection::InvertSelection()
{
	FLidarPointCloudEditorHelper::InvertSelection();
}

void ULidarToolActions_Selection::ClearSelection()
{
	FLidarPointCloudEditorHelper::ClearSelection();
}

void ULidarToolActions_Selection::BuildStaticMesh()
{
	FLidarPointCloudEditorHelper::MeshSelected(true, MaxMeshingError, bMergeMeshes, !bMergeMeshes && bRetainTransform);
}

void ULidarEditorTool_SelectionBase::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	if(Clicks.Num() == 0)
	{
		return;
	}

	const FLinearColor Color = GetHUDColor();
	
	for(int32 i = 1; i < Clicks.Num(); ++i)
	{
		FCanvasLineItem LineItem(Clicks[i - 1] / Canvas->GetDPIScale(), Clicks[i] / Canvas->GetDPIScale());
		LineItem.SetColor(Color);
		Canvas->DrawItem(LineItem);
	}
	
	FCanvasLineItem LineItem(CurrentMousePos / Canvas->GetDPIScale(), Clicks.Last() / Canvas->GetDPIScale());
	LineItem.SetColor(Color);
	Canvas->DrawItem(LineItem);
}

TArray<FConvexVolume> ULidarEditorTool_SelectionBase::GetSelectionConvexVolumes()
{
	return FLidarPointCloudEditorHelper::BuildConvexVolumesFromPoints(Clicks);
}

TObjectPtr<UInteractiveToolPropertySet> ULidarEditorTool_SelectionBase::CreateToolActions()
{
	return NewObject<ULidarToolActions_Selection>(this);
}

bool ULidarEditorTool_SelectionBase::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	CurrentMousePos = DevicePos.ScreenPosition;
	PostCurrentMousePosChanged();
	return true;
}

void ULidarEditorTool_SelectionBase::OnClickPress(const FInputDeviceRay& PressPos)
{
	bSelecting = true;
}

void ULidarEditorTool_SelectionBase::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	bSelecting = false;
}

void ULidarEditorTool_SelectionBase::OnClickDrag(const FInputDeviceRay& DragPos)
{
	CurrentMousePos = DragPos.ScreenPosition;
	PostCurrentMousePosChanged();
}

void ULidarEditorTool_SelectionBase::OnTerminateDragSequence()
{
	Clicks.Empty();
	bSelecting = false;
}

bool ULidarEditorTool_SelectionBase::ExecuteNestedCancelCommand()
{
	OnTerminateDragSequence();
	return true;
}

FText ULidarEditorTool_SelectionBase::GetToolMessage() const
{
	const FText ToolMessage = LOCTEXT("ULidarEditorToolToolMessage", "Use Left-click to start the selection. Hold Shift to add selection, hold Ctrl to subtract selection.");
	return ToolMessage;
}

FLinearColor ULidarEditorTool_SelectionBase::GetHUDColor()
{
	return FLinearColor::White;
}

void ULidarEditorTool_SelectionBase::FinalizeSelection()
{
	const ELidarPointCloudSelectionMode SelectionMode = GetSelectionMode();

	if(SelectionMode == ELidarPointCloudSelectionMode::None)
	{
		FLidarPointCloudEditorHelper::ClearSelection();
	}
	
	const TArray<FConvexVolume> ConvexVolumes = GetSelectionConvexVolumes();
	for (int32 i = 0; i < ConvexVolumes.Num(); ++i)
	{
		// Consecutive shapes need to be additive
		FLidarPointCloudEditorHelper::SelectPointsByConvexVolume(ConvexVolumes[i], i > 0 ? ELidarPointCloudSelectionMode::Add : SelectionMode);
	}
}

ELidarPointCloudSelectionMode ULidarEditorTool_SelectionBase::GetSelectionMode() const
{
	ELidarPointCloudSelectionMode SelectionMode = ELidarPointCloudSelectionMode::None;
	if(bCtrlToggle)
	{
		SelectionMode = ELidarPointCloudSelectionMode::Subtract;
	}
	else if(bShiftToggle)
	{
		SelectionMode = ELidarPointCloudSelectionMode::Add;
	}
	return SelectionMode;
}

TArray<FConvexVolume> ULidarEditorTool_BoxSelection::GetSelectionConvexVolumes()
{
	return { FLidarPointCloudEditorHelper::BuildConvexVolumeFromCoordinates(Clicks[0], Clicks[2]) };
}

void ULidarEditorTool_BoxSelection::OnClickPress(const FInputDeviceRay& PressPos)
{
	Clicks.Append({
		PressPos.ScreenPosition,
		PressPos.ScreenPosition,
		PressPos.ScreenPosition,
		PressPos.ScreenPosition
	});
}

void ULidarEditorTool_BoxSelection::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if(Clicks.Num() == 0)
	{
		return;
	}
	
	Clicks[1].Y = DragPos.ScreenPosition.Y;
	Clicks[2] = DragPos.ScreenPosition;
	Clicks[3].X = DragPos.ScreenPosition.X;
}

void ULidarEditorTool_BoxSelection::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	if(Clicks.Num() == 4)
	{
		if(Clicks[0] == Clicks[2])
		{
			FLidarPointCloudEditorHelper::ClearSelection();
		}
		else
		{
			FinalizeSelection();
		}
	}
	
	Clicks.Empty();
}

void ULidarEditorTool_PolygonalSelection::OnClickPress(const FInputDeviceRay& PressPos)
{
	if(IsWithinSnap())
	{
		FinalizeSelection();
		Clicks.Empty();
	}
	else
	{
		Clicks.Add(PressPos.ScreenPosition);
	}
}

FLinearColor ULidarEditorTool_PolygonalSelection::GetHUDColor()
{
	return IsWithinSnap() ? FLinearColor::Green : Super::GetHUDColor();
}

void ULidarEditorTool_PolygonalSelection::PostCurrentMousePosChanged()
{
	if(IsWithinSnap())
	{
		CurrentMousePos = Clicks[0];
	}
}

bool ULidarEditorTool_PolygonalSelection::IsWithinSnap()
{
	return Clicks.Num() > 1 && (CurrentMousePos - Clicks[0]).SquaredLength() <= PolySnapDistanceSq;
}

void ULidarEditorTool_LassoSelection::OnClickPress(const FInputDeviceRay& PressPos)
{
	Clicks.Add(PressPos.ScreenPosition);
}

void ULidarEditorTool_LassoSelection::OnClickDrag(const FInputDeviceRay& DragPos)
{
	Super::OnClickDrag(DragPos);
	
	if(Clicks.Num() > 0 && (CurrentMousePos - Clicks.Last()).SquaredLength() >= LassoSpacingSq)
	{
		Clicks.Add(CurrentMousePos);
	}
}

void ULidarEditorTool_LassoSelection::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	if(Clicks.Num() > 1)
	{
		FinalizeSelection();
	}

	Clicks.Empty();
}

void ULidarEditorTool_PaintSelection::Setup()
{
	Super::Setup();
	BrushRadius = GetDefault<ULidarToolActions_PaintSelection>()->BrushRadius;
}

void ULidarEditorTool_PaintSelection::Render(IToolsContextRenderAPI* RenderAPI)
{
	Super::Render(RenderAPI);
	
	if(bHasHit)
	{
		DrawWireSphere(RenderAPI->GetPrimitiveDrawInterface(), (FVector)HitLocation, FLinearColor::Red, BrushRadius, 32, SDPG_World);
	}
}

void ULidarEditorTool_PaintSelection::PostCurrentMousePosChanged()
{
	const FLidarPointCloudRay Ray = FLidarPointCloudEditorHelper::MakeRayFromScreenPosition(CurrentMousePos);

	FVector3f NewHitLocation;
	bHasHit = FLidarPointCloudEditorHelper::RayTracePointClouds(Ray, 1, NewHitLocation);

	if(!bHasHit)
	{
		return;
	}
	
	const float NewDistance = FVector3f::Dist(NewHitLocation, Ray.Origin);
	const float Deviation = (NewDistance - LastHitDistance) / LastHitDistance;

	// If painting, prevent large depth changes
	// If not, query larger trace radius - if it passes the deviation test, it was a gap
	if (Deviation > PaintMaxDeviation && (bSelecting || (
						FLidarPointCloudEditorHelper::RayTracePointClouds(Ray, 6, NewHitLocation) &&
						(FVector3f::Dist(NewHitLocation, Ray.Origin) - LastHitDistance) / LastHitDistance <= PaintMaxDeviation)))
	{
		HitLocation = FVector3f(Ray.Origin + Ray.GetDirection() * LastHitDistance);
		return;
	}
	
	HitLocation = NewHitLocation;
	LastHitDistance = NewDistance;
}

void ULidarEditorTool_PaintSelection::OnClickPress(const FInputDeviceRay& PressPos)
{
	Super::OnClickPress(PressPos);

	if(GetSelectionMode() == ELidarPointCloudSelectionMode::None)
	{
		FLidarPointCloudEditorHelper::ClearSelection();
	}
	
	Paint();
}

void ULidarEditorTool_PaintSelection::OnClickDrag(const FInputDeviceRay& DragPos)
{
	Super::OnClickDrag(DragPos);
	Paint();
}

void ULidarEditorTool_PaintSelection::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if(const ULidarToolActions_PaintSelection* Actions = Cast<ULidarToolActions_PaintSelection>(PropertySet))
	{
		if(Property && Property->GetName().Equals("BrushRadius"))
		{
			BrushRadius = Actions->BrushRadius;
		}
	}
}

void ULidarEditorTool_PaintSelection::Paint()
{
	if(bHasHit)
	{
		FLidarPointCloudEditorHelper::SelectPointsBySphere(FSphere((FVector)HitLocation, BrushRadius), GetSelectionMode());
	}
}

#undef LOCTEXT_NAMESPACE
