// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mechanics/CurveControlPointsMechanic.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseGizmos/TransformGizmo.h"
#include "BaseGizmos/TransformProxy.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/PointSetComponent.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolActionSet.h"
#include "Polyline3.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"
#include "Transforms/MultiTransformer.h"

#define LOCTEXT_NAMESPACE "UCurveControlPointsMechanic"

const FText PointAdditionTransactionText = LOCTEXT("PointAddition", "Point Addition");
const FText PointDeletionTransactionText = LOCTEXT("PointDeletion", "Point Deletion");
const FText PointDeselectionTransactionText = LOCTEXT("PointDeselection", "Point Deselection");
const FText PointSelectionTransactionText = LOCTEXT("PointSelection", "Point Selection");
const FText PointMovementTransactionText = LOCTEXT("PointMovement", "Point Movement");

UCurveControlPointsMechanic::~UCurveControlPointsMechanic()
{
	checkf(PreviewGeometryActor == nullptr, TEXT("Shutdown() should be called before UCurveControlPointsMechanic is destroyed."));
}

void UCurveControlPointsMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	ClickBehavior->Modifiers.RegisterModifier(AddToSelectionModifierId, FInputDeviceState::IsShiftKeyDown);
	ClickBehavior->Modifiers.RegisterModifier(InsertPointModifierId, FInputDeviceState::IsCtrlKeyDown);
	ParentTool->AddInputBehavior(ClickBehavior);

	HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	HoverBehavior->Modifiers.RegisterModifier(InsertPointModifierId, FInputDeviceState::IsCtrlKeyDown);
	ParentTool->AddInputBehavior(HoverBehavior);

	// We use custom materials that are visible through other objects.
	// TODO: This probably should be configurable.
	DrawnControlPoints = NewObject<UPointSetComponent>();
	DrawnControlPoints->SetPointMaterial(
		LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/PointSetOverlaidComponentMaterial")));
	DrawnControlSegments = NewObject<ULineSetComponent>();
	DrawnControlSegments->SetLineMaterial(
		LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/LineSetOverlaidComponentMaterial")));
	PreviewPoint = NewObject<UPointSetComponent>();
	PreviewPoint->SetPointMaterial(
		LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/PointSetOverlaidComponentMaterial")));
	PreviewSegment = NewObject<ULineSetComponent>();
	PreviewSegment->SetLineMaterial(
		LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/LineSetOverlaidComponentMaterial")));

	SegmentsColor = FColor::Red;
	SegmentsThickness = 4.0f;
	PointsColor = FColor::Red;
	PointsSize = 8.0f;
	HoverColor = FColor::Green;
	SelectedColor = FColor::Yellow;
	PreviewColor = HoverColor;
	DepthBias = 1.0f;

	PointsWithinToleranceTest = [this](const FVector3d& Position1, const FVector3d& Position2) {
		if (CameraState.bIsOrthographic)
		{
			// We could just always use ToolSceneQueriesUtil::PointSnapQuery. But in ortho viewports, we happen to know
			// that the only points that we will ever give this function will be the closest points between a ray and
			// some geometry, meaning that the vector between them will be orthogonal to the view ray. With this knowledge,
			// we can do the tolerance computation more efficiently than PointSnapQuery can, since we don't need to project
			// down to the view plane.
			// As in PointSnapQuery, we convert our angle-based tolerance to one we can use in an ortho viewport (instead of
			// dividing our field of view into 90 visual angle degrees, we divide the plane into 90 units).
			float OrthoTolerance = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD() * CameraState.OrthoWorldCoordinateWidth / 90.0;
			return Position1.DistanceSquared(Position2) < OrthoTolerance * OrthoTolerance;
		}
		else
		{
			return ToolSceneQueriesUtil::PointSnapQuery(CameraState, Position1, Position2);
		}
	};

	UInteractiveGizmoManager* GizmoManager = GetParentTool()->GetToolManager()->GetPairedGizmoManager();
	PointTransformProxy = NewObject<UTransformProxy>(this);
	PointTransformGizmo = GizmoManager->CreateCustomTransformGizmo(
		ETransformGizmoSubElements::TranslateAxisX | ETransformGizmoSubElements::TranslateAxisY | ETransformGizmoSubElements::TranslatePlaneXY,
		GetParentTool());
	PointTransformProxy->OnTransformChanged.AddUObject(this, &UCurveControlPointsMechanic::GizmoTransformChanged);
	PointTransformProxy->OnBeginTransformEdit.AddUObject(this, &UCurveControlPointsMechanic::GizmoTransformStarted);
	PointTransformProxy->OnEndTransformEdit.AddUObject(this, &UCurveControlPointsMechanic::GizmoTransformEnded);
	PointTransformGizmo->SetActiveTarget(PointTransformProxy);
	PointTransformGizmo->SetVisibility(false);

	// We force the coordinate system to be local so that the gizmo only moves in the plane we specify
	PointTransformGizmo->bUseContextCoordinateSystem = false;
	PointTransformGizmo->CurrentCoordinateSystem = EToolContextCoordinateSystem::Local;
}

void UCurveControlPointsMechanic::SetWorld(UWorld* World)
{
	// It may be unreasonable to worry about SetWorld being called more than once, but let's be safe anyway
	if (PreviewGeometryActor)
	{
		PreviewGeometryActor->Destroy();
	}

	// We need the world so we can create the geometry actor in the right place.
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters SpawnInfo;
	PreviewGeometryActor = World->SpawnActor<APreviewGeometryActor>(FVector::ZeroVector, Rotation, SpawnInfo);

	// Attach the rendering components to the actor
	DrawnControlPoints->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	PreviewGeometryActor->SetRootComponent(DrawnControlPoints);
	if (DrawnControlPoints->IsRegistered())
	{
		DrawnControlPoints->ReregisterComponent();
	}
	else
	{
		DrawnControlPoints->RegisterComponent();
	}

	DrawnControlSegments->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	DrawnControlSegments->AttachToComponent(DrawnControlPoints, FAttachmentTransformRules::KeepWorldTransform);
	if (DrawnControlSegments->IsRegistered())
	{
		DrawnControlSegments->ReregisterComponent();
	}
	else
	{
		DrawnControlSegments->RegisterComponent();
	}

	PreviewPoint->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	PreviewPoint->AttachToComponent(DrawnControlPoints, FAttachmentTransformRules::KeepWorldTransform);
	if (PreviewPoint->IsRegistered())
	{
		PreviewPoint->ReregisterComponent();
	}
	else
	{
		PreviewPoint->RegisterComponent();
	}

	PreviewSegment->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	PreviewSegment->AttachToComponent(DrawnControlPoints, FAttachmentTransformRules::KeepWorldTransform);
	if (PreviewSegment->IsRegistered())
	{
		PreviewSegment->ReregisterComponent();
	}
	else
	{
		PreviewSegment->RegisterComponent();
	}
}

void UCurveControlPointsMechanic::Shutdown()
{
	if (PreviewGeometryActor)
	{
		PreviewGeometryActor->Destroy();
		PreviewGeometryActor = nullptr;
	}

	if (PointTransformGizmo)
	{
		PointTransformGizmo->Shutdown();
		PointTransformGizmo = nullptr;
	}
}

void UCurveControlPointsMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	// TODO: Should we cache the camera state here or somewhere else?
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
}

void UCurveControlPointsMechanic::Initialize(const TArray<FVector3d>& Points, bool bIsLoopIn)
{
	ClearPoints();

	for (const FVector3d& Point : Points)
	{
		AppendPoint(Point);
	}

	bIsLoop = bIsLoopIn;
}

void UCurveControlPointsMechanic::ClearPoints()
{
	ClearSelection();
	ClearHover();

	ControlPoints.Empty();
	GeometrySet.Reset();
	DrawnControlSegments->Clear();
	DrawnControlPoints->Clear();
}

int32 UCurveControlPointsMechanic::AppendPoint(const FVector3d& Point)
{
	return InsertPointAt(ControlPoints.Num(), Point);
}

int32 UCurveControlPointsMechanic::InsertPointAt(int32 SequencePosition, const FVector3d& NewPointCoordinates, const int32* KnownPointID)
{
	// Add the point
	int32 NewPointID = ControlPoints.InsertPointAt(SequencePosition, NewPointCoordinates, KnownPointID);
	GeometrySet.AddPoint(NewPointID, NewPointCoordinates);
	FRenderablePoint RenderablePoint((FVector)NewPointCoordinates, PointsColor, PointsSize);
	DrawnControlPoints->InsertPoint(NewPointID, RenderablePoint);

	// See if we need to add some segments
	if (ControlPoints.Num() > 1)
	{
		if (bIsLoop || SequencePosition != 0)
		{
			// Alter (or add) the preceding segment to go to the new point.
			int32 PreviousSequencePosition = (SequencePosition + ControlPoints.Num() - 1) % ControlPoints.Num();
			int32 PreviousID = ControlPoints.GetPointIDAt(PreviousSequencePosition);

			FPolyline3d SegmentPolyline(TArray<FVector3d>{ControlPoints.GetPointCoordinates(PreviousID), NewPointCoordinates});

			if (DrawnControlSegments->IsLineValid(PreviousID))
			{
				DrawnControlSegments->SetLineEnd(PreviousID, (FVector)NewPointCoordinates);

				GeometrySet.UpdateCurve(PreviousID, SegmentPolyline);
			}
			else
			{
				FRenderableLine RenderableSegment((FVector)ControlPoints.GetPointCoordinates(PreviousID),
					(FVector)NewPointCoordinates, SegmentsColor, SegmentsThickness, DepthBias);
				DrawnControlSegments->InsertLine(PreviousID, RenderableSegment);

				GeometrySet.AddCurve(PreviousID, SegmentPolyline);
			}
		}
		if (bIsLoop || SequencePosition != ControlPoints.Num() - 1)
		{
			// Create a segment going to the next point
			int32 NextSequencePosition = (SequencePosition + 1) % ControlPoints.Num();

			FPolyline3d SegmentPolyline(TArray<FVector3d>{ControlPoints.GetPointCoordinatesAt(NextSequencePosition), NewPointCoordinates});
			GeometrySet.AddCurve(NewPointID, SegmentPolyline);

			FRenderableLine RenderableSegment((FVector)NewPointCoordinates, (FVector)ControlPoints.GetPointCoordinatesAt(NextSequencePosition),
				SegmentsColor, SegmentsThickness, DepthBias);
			DrawnControlSegments->InsertLine(NewPointID, RenderableSegment);
		}
	}

	return NewPointID;
}

void UCurveControlPointsMechanic::SetIsLoop(bool bIsLoopIn)
{
	if (bIsLoop && !bIsLoopIn)
	{
		// Need to remove the loop closing segment
		GeometrySet.RemoveCurve(ControlPoints.Last());
		DrawnControlSegments->RemoveLine(ControlPoints.Last());

		bIsLoop = bIsLoopIn;
	}
	else if (!bIsLoop && bIsLoopIn)
	{
		FPolyline3d SegmentPolyline(TArray<FVector3d>{ControlPoints.GetPointCoordinates(ControlPoints.Last()), ControlPoints.GetPointCoordinates(ControlPoints.First())});
		GeometrySet.AddCurve(ControlPoints.Last(), SegmentPolyline);

		FRenderableLine RenderableSegment((FVector)ControlPoints.GetPointCoordinates(ControlPoints.Last()),
			(FVector)ControlPoints.GetPointCoordinates(ControlPoints.First()), SegmentsColor, SegmentsThickness, DepthBias);
		DrawnControlSegments->InsertLine(ControlPoints.Last(), RenderableSegment);

		bIsLoop = bIsLoopIn;
	}
}

void UCurveControlPointsMechanic::ExtractPointPositions(TArray<FVector3d>& PositionsOut)
{
	for (int32 PointID : ControlPoints.PointIDItr())
	{
		PositionsOut.Add(ControlPoints.GetPointCoordinates(PointID));
	}
}


void UCurveControlPointsMechanic::GizmoTransformStarted(UTransformProxy* Proxy)
{
	GizmoStartPosition = Proxy->GetTransform().GetTranslation();

	ParentTool->GetToolManager()->BeginUndoTransaction(PointMovementTransactionText);
}

void UCurveControlPointsMechanic::GizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	if (SelectedPointIDs.Num() == 0)
	{
		return;
	}

	FVector Displacement = Transform.GetTranslation() - GizmoStartPosition;
	if (Displacement != FVector::ZeroVector)
	{
		for (int32 i = 0; i < SelectedPointIDs.Num(); ++i)
		{
			UpdatePointLocation(SelectedPointIDs[i], SelectedPointStartPositions[i] + Displacement);
		}
	}

	OnPointsChanged.Broadcast();
}

void UCurveControlPointsMechanic::GizmoTransformEnded(UTransformProxy* Proxy)
{
	for (int32 i = 0; i < SelectedPointIDs.Num(); ++i)
	{
		ParentTool->GetToolManager()->EmitObjectChange(this, MakeUnique<FCurveControlPointsMechanicMovementChange>(
			SelectedPointIDs[i], SelectedPointStartPositions[i], ControlPoints.GetPointCoordinates(SelectedPointIDs[i]),
			CurrentChangeStamp), PointMovementTransactionText);

		SelectedPointStartPositions[i] = ControlPoints.GetPointCoordinates(SelectedPointIDs[i]);
	}

	ParentTool->GetToolManager()->EndUndoTransaction();
}

void UCurveControlPointsMechanic::UpdatePointLocation(int32 PointID, const FVector3d& NewLocation)
{
	ControlPoints.SetPointCoordinates(PointID, NewLocation);
	GeometrySet.UpdatePoint(PointID, NewLocation);
	DrawnControlPoints->SetPointPosition(PointID, (FVector)NewLocation);

	int32 SequencePosition = ControlPoints.GetSequencePosition(PointID);
	
	// Update the segment going to this point.
	if (bIsLoop || PointID != ControlPoints.First())
	{
		int32 PreviousSequencePosition = (SequencePosition + ControlPoints.Num() - 1) % ControlPoints.Num();
		DrawnControlSegments->SetLineEnd(ControlPoints.GetPointIDAt(PreviousSequencePosition), (FVector)NewLocation);

		FPolyline3d SegmentPolyline(TArray<FVector3d>{ControlPoints.GetPointCoordinatesAt(PreviousSequencePosition), NewLocation});
		GeometrySet.UpdateCurve(ControlPoints.GetPointIDAt(PreviousSequencePosition), SegmentPolyline);
	}

	// Update the segment going from this point.
	if (bIsLoop || PointID != ControlPoints.Last())
	{
		DrawnControlSegments->SetLineStart(PointID, (FVector)NewLocation);

		FPolyline3d SegmentPolyline(TArray<FVector3d>{NewLocation, ControlPoints.GetPointCoordinatesAt((SequencePosition + 1) % ControlPoints.Num())});
		GeometrySet.UpdateCurve(PointID, SegmentPolyline);
	}
}


bool UCurveControlPointsMechanic::HitTest(const FInputDeviceRay& ClickPos, FInputRayHit& ResultOut)
{
	FGeometrySet3::FNearest Nearest;

	// If we have one of the endpoints selected and are in insert mode, we're looking for an intersection with the draw plane
	if (bInsertPointToggle && !bIsLoop && SelectedPointIDs.Num() == 1
		&& (SelectedPointIDs[0] == ControlPoints.First() || SelectedPointIDs[0] == ControlPoints.Last()))
	{
		FVector3d HitPoint;
		bool bHit = DrawPlane.RayPlaneIntersection(ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction, 2, HitPoint);
		ResultOut = FInputRayHit(ClickPos.WorldRay.GetParameter((FVector)HitPoint));
		return bHit;
	}
	// Otherwise, see if we are in insert mode and hitting a segment
	else if (bInsertPointToggle)
	{
		if (GeometrySet.FindNearestCurveToRay(ClickPos.WorldRay, Nearest, PointsWithinToleranceTest))
		{
			ResultOut = FInputRayHit(Nearest.RayParam);
			return true;
		}
	}
	// See if we hit a point
	else if (GeometrySet.FindNearestPointToRay(ClickPos.WorldRay, Nearest, PointsWithinToleranceTest))
	{
		ResultOut = FInputRayHit(Nearest.RayParam);
		return true;
	}
	return false;
}

FInputRayHit UCurveControlPointsMechanic::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FInputRayHit Result;
	HitTest(ClickPos, Result);
	return Result;
}

void UCurveControlPointsMechanic::OnClicked(const FInputDeviceRay& ClickPos)
{
	FGeometrySet3::FNearest Nearest;
	if (bInsertPointToggle)
	{
		// Adding on an existing edge takes priority to adding to the end.
		if (GeometrySet.FindNearestCurveToRay(ClickPos.WorldRay, Nearest, PointsWithinToleranceTest))
		{
			ParentTool->GetToolManager()->BeginUndoTransaction(PointAdditionTransactionText);

			int32 SequencePosition = ControlPoints.GetSequencePosition(Nearest.ID);
			int32 NewPointID = InsertPointAt(SequencePosition + 1, Nearest.NearestGeoPoint);
			ParentTool->GetToolManager()->EmitObjectChange(this, MakeUnique<FCurveControlPointsMechanicInsertionChange>(
				SequencePosition + 1, NewPointID, Nearest.NearestGeoPoint, true, CurrentChangeStamp), PointAdditionTransactionText);

			ChangeSelection(NewPointID, false);

			ParentTool->GetToolManager()->EndUndoTransaction();
			OnPointsChanged.Broadcast();
		}
		else if (SelectedPointIDs.Num() == 1 && !bIsLoop)
		{
			// Try to add to one of the ends
			if (SelectedPointIDs[0] == ControlPoints.First())
			{
				ParentTool->GetToolManager()->BeginUndoTransaction(PointAdditionTransactionText);

				FVector3d NewPointCoordinates;
				DrawPlane.RayPlaneIntersection(ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction, 2, NewPointCoordinates);
				int32 NewPointID = InsertPointAt(0, NewPointCoordinates);
				ParentTool->GetToolManager()->EmitObjectChange(this, MakeUnique<FCurveControlPointsMechanicInsertionChange>(
					0, NewPointID, NewPointCoordinates, true, CurrentChangeStamp), PointAdditionTransactionText);

				ChangeSelection(NewPointID, false);

				ParentTool->GetToolManager()->EndUndoTransaction();
				OnPointsChanged.Broadcast();
			}
			else if (SelectedPointIDs[0] == ControlPoints.Last())
			{
				ParentTool->GetToolManager()->BeginUndoTransaction(PointAdditionTransactionText);

				FVector3d NewPointCoordinates;
				DrawPlane.RayPlaneIntersection(ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction, 2, NewPointCoordinates);
				int32 NewPointID = InsertPointAt(ControlPoints.Num(), NewPointCoordinates);
				ParentTool->GetToolManager()->EmitObjectChange(this, MakeUnique<FCurveControlPointsMechanicInsertionChange>(
					ControlPoints.Num() - 1, NewPointID, NewPointCoordinates, true, CurrentChangeStamp), PointAdditionTransactionText);

				ChangeSelection(NewPointID, false);

				ParentTool->GetToolManager()->EndUndoTransaction();
				OnPointsChanged.Broadcast();
			}
		}
	}
	// Otherwise, check for plain old selection
	else if (GeometrySet.FindNearestPointToRay(ClickPos.WorldRay, Nearest, PointsWithinToleranceTest))
	{
		ParentTool->GetToolManager()->BeginUndoTransaction(PointSelectionTransactionText);

		ChangeSelection(Nearest.ID, bAddToSelectionToggle);

		ParentTool->GetToolManager()->EndUndoTransaction();
	}
}

void UCurveControlPointsMechanic::ChangeSelection(int32 NewPointID, bool bAddToSelection)
{
	// If not adding to selection, clear it
	if (!bAddToSelection && SelectedPointIDs.Num() > 0)
	{
		for (int32 PointID : SelectedPointIDs)
		{
			// We check for validity here because we'd like to be able to use this function to deselect points after
			// deleting them.
			if (DrawnControlPoints->IsPointValid(PointID))
			{
				DrawnControlPoints->SetPointColor(PointID, PointsColor);

				ParentTool->GetToolManager()->EmitObjectChange(this, MakeUnique<FCurveControlPointsMechanicSelectionChange>(
					PointID, false, CurrentChangeStamp), PointDeselectionTransactionText);
			}
		}

		SelectedPointIDs.Empty();
		SelectedPointStartPositions.Empty();
	}

	// We check for validity here because giving an invalid id (such as -1) with bAddToSelection == false
	// is an easy way to clear the selection.
	if (ControlPoints.IsValidPoint(NewPointID))
	{
		if (bAddToSelection && DeselectPoint(NewPointID))
		{
			ParentTool->GetToolManager()->EmitObjectChange(this, MakeUnique<FCurveControlPointsMechanicSelectionChange>(
				NewPointID, false, CurrentChangeStamp), PointDeselectionTransactionText);
		}
		else
		{
			SelectPoint(NewPointID);

			ParentTool->GetToolManager()->EmitObjectChange(this, MakeUnique<FCurveControlPointsMechanicSelectionChange>(
				NewPointID, true, CurrentChangeStamp), PointSelectionTransactionText);
		}
	}

	UpdateGizmoLocation();
}

void UCurveControlPointsMechanic::UpdateGizmoLocation()
{
	if (!PointTransformGizmo)
	{
		return;
	}

	if (SelectedPointIDs.Num() == 0)
	{
		PointTransformGizmo->SetVisibility(false);
	}
	else
	{
		FVector3d NewGizmoLocation;
		for (int32 PointID : SelectedPointIDs)
		{
			NewGizmoLocation += ControlPoints.GetPointCoordinates(PointID);
		}
		NewGizmoLocation /= SelectedPointIDs.Num();

		PointTransformGizmo->ReinitializeGizmoTransform(FTransform((FQuat)DrawPlane.Rotation, (FVector)NewGizmoLocation));
		PointTransformGizmo->SetVisibility(true);
	}
}

void UCurveControlPointsMechanic::SetPlane(const FFrame3d& DrawPlaneIn)
{
	DrawPlane = DrawPlaneIn;
	UpdateGizmoLocation();
}

bool UCurveControlPointsMechanic::DeselectPoint(int32 PointID)
{
	int32 IndexInSelection;
	if (SelectedPointIDs.Find(PointID, IndexInSelection))
	{
		SelectedPointIDs.RemoveAt(IndexInSelection);
		SelectedPointStartPositions.RemoveAt(IndexInSelection);
		DrawnControlPoints->SetPointColor(PointID, PointsColor);
		return true;
	}
	
	return false;
}

void UCurveControlPointsMechanic::SelectPoint(int32 PointID)
{
	SelectedPointIDs.Add(PointID);
	SelectedPointStartPositions.Add(ControlPoints.GetPointCoordinates(PointID));
	DrawnControlPoints->SetPointColor(PointID, SelectedColor);
}

void UCurveControlPointsMechanic::ClearSelection()
{
	ChangeSelection(-1, false);
}

FInputRayHit UCurveControlPointsMechanic::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FInputRayHit Result;
	HitTest(PressPos, Result);
	return Result;
}

void UCurveControlPointsMechanic::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	OnUpdateHover(DevicePos);
}

void UCurveControlPointsMechanic::ClearHover()
{
	if (HoveredPointID >= 0)
	{
		DrawnControlPoints->SetPointColor(HoveredPointID, PreHoverPointColor);
		HoveredPointID = -1;
	}
	PreviewPoint->Clear();
	PreviewSegment->Clear();
}

bool UCurveControlPointsMechanic::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	FGeometrySet3::FNearest Nearest;

	// See if we're hovering a point insertion on an existing edge
	if (bInsertPointToggle && GeometrySet.FindNearestCurveToRay(DevicePos.WorldRay, Nearest, PointsWithinToleranceTest))
	{
		ClearHover();
		FRenderablePoint RenderablePoint((FVector)Nearest.NearestGeoPoint, PreviewColor, PointsSize);
		PreviewPoint->InsertPoint(0, RenderablePoint);
	}
	// See if we're hovering a point insertion on one of the ends
	else if (bInsertPointToggle && !bIsLoop && SelectedPointIDs.Num() == 1
		&& (SelectedPointIDs[0] == ControlPoints.First() || SelectedPointIDs[0] == ControlPoints.Last()))
	{
		ClearHover();

		FVector3d HitPoint;
		if (DrawPlane.RayPlaneIntersection(DevicePos.WorldRay.Origin, DevicePos.WorldRay.Direction, 2, HitPoint))
		{
			// Redraw point and line
			FRenderablePoint RenderablePoint((FVector)HitPoint, PreviewColor, PointsSize);
			PreviewPoint->InsertPoint(0, RenderablePoint);

			FRenderableLine RenderableLine(
				(FVector)ControlPoints.GetPointCoordinates(SelectedPointIDs[0]),
				(FVector)HitPoint, PreviewColor, SegmentsThickness, DepthBias);
			PreviewSegment->InsertLine(0, RenderableLine);
		}
	}
	// See if we're hovering a point
	else if (GeometrySet.FindNearestPointToRay(DevicePos.WorldRay, Nearest, PointsWithinToleranceTest))
	{
		if (Nearest.ID != HoveredPointID)
		{
			ClearHover();
			HoveredPointID = Nearest.ID;
			PreHoverPointColor = DrawnControlPoints->GetPoint(HoveredPointID).Color;
			DrawnControlPoints->SetPointColor(HoveredPointID, HoverColor);
		}
	}
	else
	{
		return false; // Done hovering
	}

	return true;
}

void UCurveControlPointsMechanic::OnEndHover()
{
	ClearHover();
}

// Detects Ctrl and Shift key states
void UCurveControlPointsMechanic::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == AddToSelectionModifierId)
	{
		bAddToSelectionToggle = bIsOn;
	}
	else if (ModifierID == InsertPointModifierId)
	{
		bInsertPointToggle = bIsOn;
	}
}


void UCurveControlPointsMechanic::DeleteSelectedPoints()
{
	if (SelectedPointIDs.Num() == 0)
	{
		return;
	}

	ParentTool->GetToolManager()->BeginUndoTransaction(PointDeletionTransactionText);

	// There are minor inefficiencies in the way we delete multiple points since we sometimes do edge updates
	// for edges that get deleted later in the loop, and we upate the map inside ControlPoints gets updated each
	// time, but avoiding these would make the code more cumbersome.

	// For the purposes of undo/redo, it is more convenient to clear the selection before deleting the points, so that
	// on undo, the points get added back before being reselected.
	TArray<int32> PointsToDelete = SelectedPointIDs;
	ClearSelection();

	for (int32 PointID : PointsToDelete)
	{
		ParentTool->GetToolManager()->EmitObjectChange(this, MakeUnique<FCurveControlPointsMechanicInsertionChange>(
			ControlPoints.GetSequencePosition(PointID), PointID, ControlPoints.GetPointCoordinates(PointID),
			false, CurrentChangeStamp), PointDeletionTransactionText);
		DeletePoint(PointID);
	}

	ParentTool->GetToolManager()->EndUndoTransaction();
	OnPointsChanged.Broadcast();
}

int32 UCurveControlPointsMechanic::DeletePoint(int32 PointID)
{
	int32 SequencePosition = ControlPoints.GetSequencePosition(PointID);

	// Deal with the segments:
	// See if there is a preceding point
	if (ControlPoints.Num() > 1 && (bIsLoop || SequencePosition > 0))
	{
		int32 PreviousPointID = ControlPoints.GetPointIDAt((SequencePosition + ControlPoints.Num() - 1) % ControlPoints.Num());

		// See if there is a point to connect to after the about-to-be-deleted one
		if (ControlPoints.Num() > 2 && (bIsLoop || SequencePosition < ControlPoints.Num() - 1))
		{
			// Move edge
			FVector3d NextPointCoordinates = ControlPoints.GetPointCoordinatesAt((SequencePosition + 1) % ControlPoints.Num());

			DrawnControlSegments->SetLineEnd(PreviousPointID, (FVector)NextPointCoordinates);
			FPolyline3d SegmentPolyline(TArray<FVector3d>{ControlPoints.GetPointCoordinates(PreviousPointID), NextPointCoordinates});
			GeometrySet.UpdateCurve(PreviousPointID, SegmentPolyline);
		}
		else
		{
			// Delete edge
			GeometrySet.RemoveCurve(PreviousPointID);
			DrawnControlSegments->RemoveLine(PreviousPointID);
		}
	}

	// Delete outgoing edge if there is one.
	if (DrawnControlSegments->IsLineValid(PointID))
	{
		GeometrySet.RemoveCurve(PointID);
		DrawnControlSegments->RemoveLine(PointID);
	}

	// Delete the point itself.
	GeometrySet.RemovePoint(PointID);
	DrawnControlPoints->RemovePoint(PointID);
	ControlPoints.RemovePointAt(SequencePosition);

	return PointID;
}


// ==================== Undo/redo object functions ====================

FCurveControlPointsMechanicSelectionChange::FCurveControlPointsMechanicSelectionChange(int32 PointIDIn, 
	bool AddedIn, int32 ChangeStampIn)
	: PointID(PointIDIn)
	, Added(AddedIn)
	, ChangeStamp(ChangeStampIn)
{}

void FCurveControlPointsMechanicSelectionChange::Apply(UObject* Object)
{
	UCurveControlPointsMechanic* Mechanic = Cast<UCurveControlPointsMechanic>(Object);
	if (Added)
	{
		Mechanic->SelectPoint(PointID);
	}
	else
	{
		Mechanic->DeselectPoint(PointID);
	}
	Mechanic->UpdateGizmoLocation();
}

void FCurveControlPointsMechanicSelectionChange::Revert(UObject* Object)
{
	UCurveControlPointsMechanic* Mechanic = Cast<UCurveControlPointsMechanic>(Object);
	if (Added)
	{
		Mechanic->DeselectPoint(PointID);
	}
	else
	{
		Mechanic->SelectPoint(PointID);
	}
	Mechanic->UpdateGizmoLocation();
}

FString FCurveControlPointsMechanicSelectionChange::ToString() const
{
	return TEXT("FCurveControlPointsMechanicSelectionChange");
}


FCurveControlPointsMechanicInsertionChange::FCurveControlPointsMechanicInsertionChange(int32 SequencePositionIn,
	int32 PointIDIn, const FVector3d& CoordinatesIn, bool AddedIn, int32 ChangeStampIn)
	: SequencePosition(SequencePositionIn)
	, PointID(PointIDIn)
	, Coordinates(CoordinatesIn)
	, Added(AddedIn)
	, ChangeStamp(ChangeStampIn)
{}

void FCurveControlPointsMechanicInsertionChange::Apply(UObject* Object)
{
	UCurveControlPointsMechanic* Mechanic = Cast<UCurveControlPointsMechanic>(Object);
	if (Added)
	{
		Mechanic->InsertPointAt(SequencePosition, Coordinates, &PointID);
	}
	else
	{
		Mechanic->DeletePoint(PointID);
	}
	Mechanic->OnPointsChanged.Broadcast();
}

void FCurveControlPointsMechanicInsertionChange::Revert(UObject* Object)
{
	UCurveControlPointsMechanic* Mechanic = Cast<UCurveControlPointsMechanic>(Object);
	if (Added)
	{
		Mechanic->DeletePoint(PointID);
	}
	else
	{
		Mechanic->InsertPointAt(SequencePosition, Coordinates, &PointID);
	}
	Mechanic->OnPointsChanged.Broadcast();
}

FString FCurveControlPointsMechanicInsertionChange::ToString() const
{
	return TEXT("FCurveControlPointsMechanicSelectionChange");
}


FCurveControlPointsMechanicMovementChange::FCurveControlPointsMechanicMovementChange(int32 PointIDIn,
	const FVector3d& OriginalPositionIn, const FVector3d& NewPositionIn, int32 ChangeStampIn)
	: PointID(PointIDIn)
	, OriginalPosition(OriginalPositionIn)
	, NewPosition(NewPositionIn)
	, ChangeStamp(ChangeStampIn)
{}

void FCurveControlPointsMechanicMovementChange::Apply(UObject* Object)
{
	UCurveControlPointsMechanic* Mechanic = Cast<UCurveControlPointsMechanic>(Object);
	Mechanic->UpdatePointLocation(PointID, NewPosition);
	Mechanic->OnPointsChanged.Broadcast();
}

void FCurveControlPointsMechanicMovementChange::Revert(UObject* Object)
{
	UCurveControlPointsMechanic* Mechanic = Cast<UCurveControlPointsMechanic>(Object);
	Mechanic->UpdatePointLocation(PointID, OriginalPosition);
	Mechanic->OnPointsChanged.Broadcast();
}

FString FCurveControlPointsMechanicMovementChange::ToString() const
{
	return TEXT("FCurveControlPointsMechanicMovementChange");
}


// ==================== FOrderedPoints functions ====================

UCurveControlPointsMechanic::FOrderedPoints::FOrderedPoints(const FOrderedPoints& ToCopy)
	: Vertices(ToCopy.Vertices)
	, Sequence(ToCopy.Sequence)
{
}

UCurveControlPointsMechanic::FOrderedPoints::FOrderedPoints(const TArray<FVector3d>& PointSequence)
{
	ReInitialize(PointSequence);
}

int32 UCurveControlPointsMechanic::FOrderedPoints::AppendPoint(const FVector3d& PointCoordinates)
{
	int32 PointID = Vertices.Add(PointCoordinates);
	Sequence.Add(PointID);
	PointIDToSequencePosition.Add(PointID, Sequence.Num()-1);
	return PointID;
}

int32 UCurveControlPointsMechanic::FOrderedPoints::InsertPointAt(int32 SequencePosition, 
	const FVector3d& VertCoordinates, const int32* KnownPointID)
{
	// Everything from this point onward moves further in the sequence, so update map
	for (int32 i = SequencePosition; i < Sequence.Num(); ++i)
	{
		++PointIDToSequencePosition[Sequence[i]];
	}

	int32 PointID;
	if (KnownPointID)
	{
		Vertices.Insert(*KnownPointID, VertCoordinates);
		PointID = *KnownPointID;
	}
	else
	{
		PointID = Vertices.Add(VertCoordinates);
	}

	Sequence.Insert(PointID, SequencePosition);
	PointIDToSequencePosition.Add(PointID, SequencePosition);
	return PointID;
}

int32 UCurveControlPointsMechanic::FOrderedPoints::RemovePointAt(int32 SequencePosition)
{
	check(SequencePosition >= 0 && SequencePosition < Sequence.Num());

	// Everything past this point moves back in sequence, so update map
	for (int32 i = SequencePosition + 1; i < Sequence.Num(); ++i)
	{
		--PointIDToSequencePosition[Sequence[i]];
	}

	int32 PointID = Sequence[SequencePosition];
	Vertices.RemoveAt(PointID);
	Sequence.RemoveAt(SequencePosition);
	PointIDToSequencePosition.Remove(PointID);

	return PointID;
}

void UCurveControlPointsMechanic::FOrderedPoints::Empty()
{
	Vertices.Empty();
	Sequence.Empty();
	PointIDToSequencePosition.Empty();
}

void UCurveControlPointsMechanic::FOrderedPoints::ReInitialize(const TArray<FVector3d>& PointSequence)
{
	Empty();

	Vertices.Reserve(PointSequence.Num());
	Sequence.Reserve(PointSequence.Num());
	PointIDToSequencePosition.Reserve(PointSequence.Num());
	for (int i = 0; i < PointSequence.Num(); ++i)
	{
		int32 PointID = Vertices.Add(PointSequence[i]);
		Sequence.Add(PointID);
		PointIDToSequencePosition.Add(PointID, i);
	}
}

#undef LOCTEXT_NAMESPACE