// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolActivities/PolyEditExtrudeActivity.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "ContextObjectStore.h"
#include "Drawing/PolyEditPreviewMesh.h"
#include "DynamicMesh/MeshNormals.h"
#include "EditMeshPolygonsTool.h"
#include "InteractiveToolManager.h"
#include "Mechanics/PlaneDistanceFromHitMechanic.h"
#include "MeshOpPreviewHelpers.h"
#include "Operations/OffsetMeshRegion.h"
#include "Selection/PolygonSelectionMechanic.h"
#include "ToolActivities/PolyEditActivityContext.h"
#include "ToolActivities/PolyEditActivityUtil.h"
#include "ToolSceneQueriesUtil.h"

#define LOCTEXT_NAMESPACE "UPolyEditExtrudeActivity"

using namespace UE::Geometry;

void UPolyEditExtrudeActivity::Setup(UInteractiveTool* ParentToolIn)
{
	Super::Setup(ParentToolIn);

	ExtrudeProperties = NewObject<UPolyEditExtrudeProperties>();
	ExtrudeProperties->RestoreProperties(ParentTool.Get());
	AddToolPropertySource(ExtrudeProperties);
	SetToolPropertySourceEnabled(ExtrudeProperties, false);
	ExtrudeProperties->WatchProperty(ExtrudeProperties->Direction,
		[this](EPolyEditExtrudeDirection) { 
			Clear();
			BeginExtrude();
		});
	ExtrudeProperties->WatchProperty(ExtrudeProperties->ExtrudeMode,
		[this](EPolyEditExtrudeMode) {
			Clear();
			BeginExtrude();
		});

	// Register ourselves to receive clicks and hover
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	ParentTool->AddInputBehavior(ClickBehavior);
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	ParentTool->AddInputBehavior(HoverBehavior);

	ActivityContext = ParentTool->GetToolManager()->GetContextObjectStore()->FindContext<UPolyEditActivityContext>();
}

void UPolyEditExtrudeActivity::Shutdown(EToolShutdownType ShutdownType)
{
	Clear();
	ExtrudeProperties->SaveProperties(ParentTool.Get());

	ExtrudeProperties = nullptr;
	ParentTool = nullptr;
	ActivityContext = nullptr;
}

bool UPolyEditExtrudeActivity::CanStart() const
{
	if (!ActivityContext)
	{
		return false;
	}
	const FGroupTopologySelection& Selection = ActivityContext->SelectionMechanic->GetActiveSelection();
	return !Selection.SelectedGroupIDs.IsEmpty();
}

EToolActivityStartResult UPolyEditExtrudeActivity::Start()
{
	if (!CanStart())
	{
		ParentTool->GetToolManager()->DisplayMessage(
			LOCTEXT("OnExtrudeFailedMesssage", "Cannot extrude without face selection."),
			EToolMessageLevel::UserWarning);
		return EToolActivityStartResult::FailedStart;
	}

	Clear();
	BeginExtrude();
	bIsRunning = true;

	ActivityContext->EmitActivityStart(LOCTEXT("BeginExtrudeActivity", "Begin Extrude"));

	return EToolActivityStartResult();
}

bool UPolyEditExtrudeActivity::CanAccept() const
{
	return false;
}

EToolActivityEndResult UPolyEditExtrudeActivity::End(EToolShutdownType ShutdownType)
{
	if (!bIsRunning)
	{
		Clear();
		return EToolActivityEndResult::ErrorDuringEnd;
	}

	if (ShutdownType == EToolShutdownType::Cancel)
	{
		Clear();
		bIsRunning = false;
		return EToolActivityEndResult::Cancelled;
	}
	else
	{
		ApplyExtrude();
		Clear();
		bIsRunning = false;
		return EToolActivityEndResult::Completed;
	}
}


void UPolyEditExtrudeActivity::BeginExtrude()
{
	const FGroupTopologySelection& ActiveSelection = ActivityContext->SelectionMechanic->GetActiveSelection();
	TArray<int32> ActiveTriangleSelection;
	ActivityContext->CurrentTopology->GetSelectedTriangles(ActiveSelection, ActiveTriangleSelection);

	FTransform3d WorldTransform(ActivityContext->Preview->PreviewMesh->GetTransform());

	// Get the world frame
	FFrame3d ActiveSelectionFrameLocal = ActivityContext->CurrentTopology->GetSelectionFrame(ActiveSelection);
	ActiveSelectionFrameWorld = ActiveSelectionFrameLocal;
	ActiveSelectionFrameWorld.Transform(WorldTransform);
	ActiveSelectionFrameWorld.AlignAxis(2, GetExtrudeDirection());

	// Set up a preview of the extruded portion of the mesh
	EditPreview = PolyEditActivityUtil::CreatePolyEditPreviewMesh(*ParentTool, *ActivityContext);
	EditPreview->InitializeExtrudeType(ActivityContext->CurrentMesh.Get(), ActiveTriangleSelection, ActiveSelectionFrameWorld.Z(), &WorldTransform, true);
	// move world extrude frame to point on surface
	ActiveSelectionFrameWorld.Origin = EditPreview->GetInitialPatchMeshSpatial().FindNearestPoint(ActiveSelectionFrameWorld.Origin);

	// Hide the selected triangles (that are being replaced by the extruded portion)
	ActivityContext->Preview->PreviewMesh->SetSecondaryBuffersVisibility(false);

	// Set up the mechanic we use to determine how far to extrude
	ExtrudeHeightMechanic = NewObject<UPlaneDistanceFromHitMechanic>(this);
	ExtrudeHeightMechanic->Setup(ParentTool.Get());
	ExtrudeHeightMechanic->WorldHitQueryFunc = [this](const FRay& WorldRay, FHitResult& HitResult)
	{
		return ToolSceneQueriesUtil::FindNearestVisibleObjectHit(ActivityContext->Preview->GetWorld(), HitResult, WorldRay);
	};
	ExtrudeHeightMechanic->WorldPointSnapFunc = [this](const FVector3d& WorldPos, FVector3d& SnapPos)
	{
		return ActivityContext->CommonProperties->bSnapToWorldGrid && ToolSceneQueriesUtil::FindWorldGridSnapPoint(ParentTool.Get(), WorldPos, SnapPos);
	};
	ExtrudeHeightMechanic->CurrentHeight = 1.0f;  // initialize to something non-zero...prob should be based on polygon bounds maybe?

	// make inifinite-extent hit-test mesh to use in the mechanic
	FDynamicMesh3 ExtrudeHitTargetMesh;
	EditPreview->MakeExtrudeTypeHitTargetMesh(ExtrudeHitTargetMesh);
	ExtrudeHeightMechanic->Initialize(MoveTemp(ExtrudeHitTargetMesh), ActiveSelectionFrameWorld, true);

	SetToolPropertySourceEnabled(ExtrudeProperties, true);

	float BoundsMaxDim = ActivityContext->CurrentMesh->GetBounds().MaxDim();
	if (BoundsMaxDim > 0)
	{
		UVScaleFactor = 1.0 / BoundsMaxDim;
	}

	bPreviewUpdatePending = true;
}

void UPolyEditExtrudeActivity::ApplyExtrude()
{
	check(ExtrudeHeightMechanic != nullptr && EditPreview != nullptr);

	const FGroupTopologySelection& ActiveSelection = ActivityContext->SelectionMechanic->GetActiveSelection();
	TArray<int32> ActiveTriangleSelection;
	ActivityContext->CurrentTopology->GetSelectedTriangles(ActiveSelection, ActiveTriangleSelection);

	FTransform3d WorldTransform(ActivityContext->Preview->PreviewMesh->GetTransform());
	FVector3d MeshSpaceExtrudeDirection = WorldTransform.InverseTransformVector(ActiveSelectionFrameWorld.Z());
	double ExtrudeDistance = ExtrudeHeightMechanic->CurrentHeight;

	FOffsetMeshRegion Extruder(ActivityContext->CurrentMesh.Get());
	Extruder.UVScaleFactor = UVScaleFactor;
	Extruder.Triangles = ActiveTriangleSelection;
	TSet<int32> TriangleSet(ActiveTriangleSelection);
	bool bUseNormals = (ExtrudeProperties->ExtrudeMode != EPolyEditExtrudeMode::SingleDirection);
	Extruder.OffsetPositionFunc = [ExtrudeDistance, bUseNormals, &MeshSpaceExtrudeDirection](const FVector3d& Pos, const FVector3f& Normal, int32 VertexID) {
		return Pos + ExtrudeDistance * (bUseNormals ? (FVector3d)Normal : MeshSpaceExtrudeDirection);
	};
	Extruder.bIsPositiveOffset = (ExtrudeDistance > 0);
	Extruder.bUseFaceNormals = (ExtrudeProperties->ExtrudeMode == EPolyEditExtrudeMode::SelectedTriangleNormals);
	Extruder.bOffsetFullComponentsAsSolids = ExtrudeProperties->bShellsToSolids;
	Extruder.ChangeTracker = MakeUnique<FDynamicMeshChangeTracker>(ActivityContext->CurrentMesh.Get());
	Extruder.ChangeTracker->BeginChange();
	Extruder.Apply();

	FMeshNormals::QuickComputeVertexNormalsForTriangles(*ActivityContext->CurrentMesh, Extruder.AllModifiedTriangles);

	// construct new selection
	FGroupTopologySelection NewSelection;
	if (!ActivityContext->bTriangleMode)
	{
		for (const FOffsetMeshRegion::FOffsetInfo& Info : Extruder.OffsetRegions)
		{
			NewSelection.SelectedGroupIDs.Append(Info.OffsetGroups);
		}
	}
	else
	{
		for (const FOffsetMeshRegion::FOffsetInfo& Info : Extruder.OffsetRegions)
		{
			NewSelection.SelectedGroupIDs.Append(Info.InitialTriangles);
		}
	}

	// Emit undo  (also updates relevant structures)
	ActivityContext->EmitCurrentMeshChangeAndUpdate(LOCTEXT("PolyMeshExtrudeChange", "Extrude"),
		Extruder.ChangeTracker->EndChange(), NewSelection, true);
}

void UPolyEditExtrudeActivity::Clear()
{
	if (EditPreview != nullptr)
	{
		EditPreview->Disconnect();
		EditPreview = nullptr;
	}

	ActivityContext->Preview->PreviewMesh->SetSecondaryBuffersVisibility(true);

	ExtrudeHeightMechanic = nullptr;
	SetToolPropertySourceEnabled(ExtrudeProperties, false);
}


FVector3d UPolyEditExtrudeActivity::GetExtrudeDirection() const
{
	switch (ExtrudeProperties->Direction)
	{
	default:
	case EPolyEditExtrudeDirection::SelectionNormal:
		return ActiveSelectionFrameWorld.Z();
	case EPolyEditExtrudeDirection::WorldX:
		return FVector3d::UnitX();
	case EPolyEditExtrudeDirection::WorldY:
		return FVector3d::UnitY();
	case EPolyEditExtrudeDirection::WorldZ:
		return FVector3d::UnitZ();
	case EPolyEditExtrudeDirection::LocalX:
		return FTransform3d(ActivityContext->Preview->PreviewMesh->GetTransform()).GetRotation().AxisX();
	case EPolyEditExtrudeDirection::LocalY:
		return FTransform3d(ActivityContext->Preview->PreviewMesh->GetTransform()).GetRotation().AxisY();
	case EPolyEditExtrudeDirection::LocalZ:
		return FTransform3d(ActivityContext->Preview->PreviewMesh->GetTransform()).GetRotation().AxisZ();
	}
	return ActiveSelectionFrameWorld.Z();
}

void UPolyEditExtrudeActivity::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (ExtrudeHeightMechanic != nullptr)
	{
		ExtrudeHeightMechanic->Render(RenderAPI);
	}
}

void UPolyEditExtrudeActivity::Tick(float DeltaTime)
{
	if (EditPreview && bPreviewUpdatePending)
	{
		switch (ExtrudeProperties->ExtrudeMode)
		{
		case EPolyEditExtrudeMode::SingleDirection:
			EditPreview->UpdateExtrudeType(ExtrudeHeightMechanic->CurrentHeight);
			break;
		case EPolyEditExtrudeMode::SelectedTriangleNormals:
			EditPreview->UpdateExtrudeType_FaceNormalAvg(ExtrudeHeightMechanic->CurrentHeight);
			break;
		case EPolyEditExtrudeMode::VertexNormals:
			EditPreview->UpdateExtrudeType(ExtrudeHeightMechanic->CurrentHeight, true);
			break;
		}

		bPreviewUpdatePending = false;
	}
}

FInputRayHit UPolyEditExtrudeActivity::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FInputRayHit OutHit;
	OutHit.bHit = bIsRunning;
	return OutHit;
}

void UPolyEditExtrudeActivity::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (bIsRunning)
	{
		ApplyExtrude();

		// End activity
		Clear();
		bIsRunning = false;
		Cast<IToolActivityHost>(ParentTool)->NotifyActivitySelfEnded(this);
	}
}

FInputRayHit UPolyEditExtrudeActivity::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FInputRayHit OutHit;
	OutHit.bHit = bIsRunning;
	return OutHit;
}

bool UPolyEditExtrudeActivity::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	ExtrudeHeightMechanic->UpdateCurrentDistance(DevicePos.WorldRay);
	bPreviewUpdatePending = true;
	return bIsRunning;
}

#undef LOCTEXT_NAMESPACE
