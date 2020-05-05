// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoleFillTool.h"
#include "ToolBuilderUtil.h"
#include "InteractiveToolManager.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "ToolSetupUtil.h"
#include "MeshNormals.h"
#include "Changes/DynamicMeshChangeTarget.h"
#include "DynamicMeshToMeshDescription.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "MeshBoundaryLoops.h"
#include "MeshOpPreviewHelpers.h"
#include "Selection/PolygonSelectionMechanic.h"

#define LOCTEXT_NAMESPACE "UHoleFillTool"

/*
 * ToolBuilder
 */

bool UHoleFillToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UHoleFillToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);

	UHoleFillTool* NewTool = NewObject<UHoleFillTool>(SceneState.ToolManager);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);

	return NewTool;
}

/*
 * Tool properties
 */
void UHoleFillToolActions::PostAction(EHoleFillToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}


/*
* Op Factory
*/

TUniquePtr<FDynamicMeshOperator> UHoleFillOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FHoleFillOp> FillOp = MakeUnique<FHoleFillOp>();

	FTransform LocalToWorld = FillTool->ComponentTarget->GetWorldTransform();
	FillOp->SetResultTransform((FTransform3d)LocalToWorld);
	FillOp->OriginalMesh = &(FillTool->OriginalMesh);
	FillOp->FillType = FillTool->Properties->FillType;
	FillOp->MeshUVScaleFactor = FillTool->MeshUVScaleFactor;

	FillTool->GetLoopsToFill(FillOp->Loops);

	return FillOp;
}


/*
 * Tool
 */

void UHoleFillTool::Setup()
{
	USingleSelectionTool::Setup();

	// initialize properties
	Properties = NewObject<UHoleFillToolProperties>(this, TEXT("Hole Fill Settings"));
	Properties->RestoreProperties(this);
	AddToolPropertySource(Properties);

	Actions = NewObject<UHoleFillToolActions>(this, TEXT("Hole Fill Actions"));
	Actions->Initialize(this);
	AddToolPropertySource(Actions);

	ToolPropertyObjects.Add(this);

	if (!ComponentTarget)
	{
		return;
	}

	// click behavior
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	// hover behavior
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	// create mesh to operate on
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), const_cast<FDynamicMesh3&>(OriginalMesh));

	// initialize hit query
	MeshSpatial.SetMesh(&OriginalMesh);

	// initialize topology
	Topology = MakeUnique<FBasicTopology>(&OriginalMesh, false);
	Topology->RebuildTopology();

	// Set up selection mechanic to find and select edges
	SelectionMechanic = NewObject<UPolygonSelectionMechanic>(this);
	SelectionMechanic->bAddSelectionFilterPropertiesToParentTool = false;
	SelectionMechanic->Setup(this);
	SelectionMechanic->Properties->bSelectEdges = true;
	SelectionMechanic->Properties->bSelectFaces = false;
	SelectionMechanic->Properties->bSelectVertices = false;
	SelectionMechanic->Initialize(&OriginalMesh,
		FTransform3d(ComponentTarget->GetWorldTransform()),
		Topology.Get(),
		[this]() { return &MeshSpatial; },
		[]() { return true; }	// allow adding to selection without modifier key
	);
	
	// Store a UV scale based on the original mesh bounds
	MeshUVScaleFactor = (1.0 / OriginalMesh.GetBounds().MaxDim());

	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreview();
	Preview->InvalidateResult();

	// Hide all meshes except the Preview
	ComponentTarget->SetOwnerVisibility(false);
}


void UHoleFillTool::OnTick(float DeltaTime)
{
	Preview->Tick(DeltaTime);

	if (bHavePendingAction)
	{
		ApplyAction(PendingAction);
		bHavePendingAction = false;
		PendingAction = EHoleFillToolActions::NoAction;
	}
}

void UHoleFillTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Preview->InvalidateResult();
}

bool UHoleFillTool::CanAccept() const
{
	return Preview->HaveValidResult();
}

void UHoleFillTool::Shutdown(EToolShutdownType ShutdownType)
{
	Properties->SaveProperties(this);

	ComponentTarget->SetOwnerVisibility(true);

	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("HoleFillToolTransactionName", "Hole Fill Tool"));

		check(Result.Mesh.Get() != nullptr);
		ComponentTarget->CommitMesh([&Result](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;

			// full conversion if normal topology changed or faces were inverted
			Converter.Convert(Result.Mesh.Get(), *CommitParams.MeshDescription);
		});

		GetToolManager()->EndUndoTransaction();
	}
}

FInputRayHit UHoleFillTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FHitResult OutHit;
	if (SelectionMechanic->TopologyHitTest(ClickPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	return FInputRayHit(TNumericLimits<float>::Max());
}

void UHoleFillTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	// update selection
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PolyMeshSelectionChange", "Selection"));
	SelectionMechanic->BeginChange();

	FVector3d LocalHitPosition, LocalHitNormal;
	bool bSelectionModified = SelectionMechanic->UpdateSelection(ClickPos.WorldRay, LocalHitPosition, LocalHitNormal);

	if (bSelectionModified)
	{
		UpdateActiveBoundaryLoopSelection();
		Preview->InvalidateResult();
	}

	SelectionMechanic->EndChangeAndEmitIfModified();
	GetToolManager()->EndUndoTransaction();
}


FInputRayHit UHoleFillTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;
	if (SelectionMechanic->TopologyHitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}
	return FInputRayHit();
}

bool UHoleFillTool::OnUpdateHover(const FInputDeviceRay& DevicePos) 
{
	SelectionMechanic->UpdateHighlight(DevicePos.WorldRay);
	return true;
}

void UHoleFillTool::OnEndHover()
{
	SelectionMechanic->ClearHighlight();
}

void UHoleFillTool::RequestAction(EHoleFillToolActions ActionType)
{
	if (bHavePendingAction)
	{
		return;
	}

	PendingAction = ActionType;
	bHavePendingAction = true;
}

void UHoleFillTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UHoleFillTool::SetupPreview()
{
	UHoleFillOperatorFactory* OpFactory = NewObject<UHoleFillOperatorFactory>();
	OpFactory->FillTool = this;

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview");
	Preview->Setup(this->TargetWorld, OpFactory);

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials(MaterialSet.Materials,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);

	// configure secondary render material
	UMaterialInterface* SelectionMaterial = ToolSetupUtil::GetSelectionMaterial(FLinearColor(0.8f, 0.75f, 0.0f), GetToolManager());
	if (SelectionMaterial != nullptr)
	{
		Preview->PreviewMesh->SetSecondaryRenderMaterial(SelectionMaterial);
	}

	// enable secondary triangle buffers
	Preview->OnOpCompleted.AddLambda(
		[this](const FDynamicMeshOperator* Op)
	{
		const FHoleFillOp* HoleFilleOp = (const FHoleFillOp*)(Op);
		NewTriangleIDs = TSet<int32>(HoleFilleOp->NewTriangles);
	});

	Preview->PreviewMesh->EnableSecondaryTriangleBuffers(
		[this](const FDynamicMesh3* Mesh, int32 TriangleID)
	{
		return NewTriangleIDs.Contains(TriangleID);
	});

	// set initial preview to un-processed mesh
	Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	Preview->PreviewMesh->UpdatePreview(&OriginalMesh);

	Preview->SetVisibility(true);
}


void UHoleFillTool::ApplyAction(EHoleFillToolActions ActionType)
{
	switch (ActionType)
	{
	case EHoleFillToolActions::SelectAll:
		SelectAll();
		break;
	case EHoleFillToolActions::ClearSelection:
		ClearSelection();
		break;
	}
}

void UHoleFillTool::SelectAll()
{
	FGroupTopologySelection NewSelection;
	for (int32 i = 0; i < Topology->Edges.Num(); ++i)
	{
		NewSelection.SelectedEdgeIDs.Add(i);
	}

	SelectionMechanic->SetSelection(NewSelection);
	UpdateActiveBoundaryLoopSelection();
	Preview->InvalidateResult();
}


void UHoleFillTool::ClearSelection()
{
	SelectionMechanic->ClearSelection();
	UpdateActiveBoundaryLoopSelection();
	Preview->InvalidateResult();
}

void UHoleFillTool::UpdateActiveBoundaryLoopSelection()
{
	ActiveBoundaryLoopSelection.Reset();

	const FGroupTopologySelection& ActiveSelection = SelectionMechanic->GetActiveSelection();
	int NumEdges = ActiveSelection.SelectedEdgeIDs.Num();
	if (NumEdges == 0)
	{
		return;
	}

	ActiveBoundaryLoopSelection.Reserve(NumEdges);
	for (int32 k = 0; k < NumEdges; ++k)
	{
		int32 EdgeID = ActiveSelection.SelectedEdgeIDs[k];
		if (Topology->IsBoundaryEdge(EdgeID))
		{
			FSelectedBoundaryLoop& Loop = ActiveBoundaryLoopSelection.Emplace_GetRef();
			Loop.EdgeTopoID = EdgeID;
			Loop.EdgeIDs = Topology->GetGroupEdgeEdges(EdgeID);
		}
	}
}


void UHoleFillTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	SelectionMechanic->Render(RenderAPI);
}


void UHoleFillTool::GetLoopsToFill(TArray<FEdgeLoop>& OutLoops) const
{
	OutLoops.Reset();

	for (const FSelectedBoundaryLoop& FillEdge : ActiveBoundaryLoopSelection)
	{
		if (OriginalMesh.IsBoundaryEdge(FillEdge.EdgeIDs[0]))		// may no longer be boundary due to previous fill
		{
			FMeshBoundaryLoops BoundaryLoops(&OriginalMesh);
			int32 LoopID = BoundaryLoops.FindLoopContainingEdge(FillEdge.EdgeIDs[0]);
			if (LoopID >= 0)
			{
				OutLoops.Add(BoundaryLoops.Loops[LoopID]);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
