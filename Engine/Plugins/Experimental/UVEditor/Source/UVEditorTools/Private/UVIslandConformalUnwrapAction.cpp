// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVIslandConformalUnwrapAction.h"

#include "Algo/Unique.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "Selection/UVEditorDynamicMeshSelection.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "PreviewMesh.h"
#include "ToolSetupUtil.h"
#include "UVToolContextObjects.h"
#include "Parameterization/DynamicMeshUVEditor.h"
#include "Selections/MeshConnectedComponents.h"

#define LOCTEXT_NAMESPACE "UUVIslandConformalUnwrapAction"

using namespace UE::Geometry;


UUVIslandConformalUnwrapAction::UUVIslandConformalUnwrapAction()
{
	CurrentSelection = MakeShared<UE::Geometry::FUVEditorDynamicMeshSelection>();
}

void UUVIslandConformalUnwrapAction::SetWorld(UWorld* WorldIn)
{
	UUVToolAction::SetWorld(WorldIn);
}

void UUVIslandConformalUnwrapAction::Setup(UInteractiveTool* ParentToolIn)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVIslandConformalUnwrapAction_Setup);

	UUVToolAction::Setup(ParentToolIn);
}

void UUVIslandConformalUnwrapAction::Shutdown()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVIslandConformalUnwrapAction_Shutdown);
}


void UUVIslandConformalUnwrapAction::SetSelection(int32 SelectionTargetIndexIn, const UE::Geometry::FUVEditorDynamicMeshSelection* NewSelection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVIslandConformalUnwrapAction_SetSelection);
	
	SelectionTargetIndex = SelectionTargetIndexIn;
	if (NewSelection)
	{
		(*CurrentSelection) = *NewSelection;
	}
	else
	{
		(*CurrentSelection) = UE::Geometry::FUVEditorDynamicMeshSelection();
	}

	UpdateVisualizations();
}

bool UUVIslandConformalUnwrapAction::PreCheckAction() 
{
	if (!CurrentSelection || CurrentSelection->Type != FUVEditorDynamicMeshSelection::EType::Triangle
		|| CurrentSelection->Mesh == nullptr || SelectionTargetIndex == INDEX_NONE)
	{
		ParentTool->GetToolManager()->DisplayMessage(
			LOCTEXT("UnwrapErrorSelectionEmpty", "Cannot perform unwrap. Mesh selection must be island triangles."),
			EToolMessageLevel::UserWarning);
		return false;
	}

	if (!GatherIslandTids())
	{
		return false;
	}

	// Todo: Add some check here to confirm we have islands and not random triangles... like completely selected connected components?

	return true;
}

bool UUVIslandConformalUnwrapAction::GatherIslandTids() {
	TRACE_CPUPROFILER_EVENT_SCOPE(UVIslandConformalUnwrapAction_GatherIslandTids);

	IslandStartIndices.Reset();
	ConcatenatedIslandTids.Reset();
	MaxIslandSize = 0;

	FDynamicMesh3& AppliedMesh = *(Targets[SelectionTargetIndex]->AppliedCanonical);
	FDynamicMeshUVOverlay* UseOverlay = AppliedMesh.Attributes()->GetUVLayer(Targets[SelectionTargetIndex]->UVLayerIndex);

	FMeshConnectedComponents ConnectedComponents(&AppliedMesh);

	ConnectedComponents.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1) {
		return UseOverlay->AreTrianglesConnected(Triangle0, Triangle1);
	});
	
	int32 NumComponents = ConnectedComponents.Num();
	IslandStartIndices.Add(0);
	for (int32 k = 0; k < NumComponents; ++k)
	{
		TSet<int32> ComponentTris = TSet<int32>(ConnectedComponents[k].Indices);
		TSet<int32> SelectedComponentTris = ComponentTris.Intersect(CurrentSelection->SelectedIDs);
		if (SelectedComponentTris.Num() == ComponentTris.Num())
		{
			ConcatenatedIslandTids.Append(SelectedComponentTris.Array());
			IslandStartIndices.Add(IslandStartIndices.Last(0) + SelectedComponentTris.Num());
			MaxIslandSize = FMath::Max(MaxIslandSize, SelectedComponentTris.Num());
		}
	}

	return true;
}

bool UUVIslandConformalUnwrapAction::ApplyAction(UUVToolEmitChangeAPI& EmitChangeAPI)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVIslandConformalUnwrapAction_ApplyAction);

	FDynamicMesh3& MeshToUnwrap = *(Targets[SelectionTargetIndex]->AppliedCanonical);
	FDynamicMeshUVOverlay* UVOverlay = MeshToUnwrap.Attributes()->GetUVLayer(Targets[SelectionTargetIndex]->UVLayerIndex);
	FDynamicMeshUVEditor UVEditor(&MeshToUnwrap, Targets[SelectionTargetIndex]->UVLayerIndex, true);

	FDynamicMeshChangeTracker ChangeTracker(Targets[SelectionTargetIndex]->UnwrapCanonical.Get());
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(ConcatenatedIslandTids, true);

	TArray<int32> ComponentTris;
	ComponentTris.Reserve(MaxIslandSize);
	int32 NumComponents = IslandStartIndices.Num()-1;
	TArray<bool> bComponentSolved;
	bComponentSolved.Init(false, NumComponents);
	int32 SuccessCount = 0;
	TSet<int32> VidSet;
	for (int32 k = 0; k < NumComponents; ++k)
	{


		ComponentTris.Reset(0);
		int32 StartIndex = IslandStartIndices[k];
		int32 Count = IslandStartIndices[k+1] - StartIndex;
		if (Count == 0)
		{
			continue; // Skip everything if there are no Tids for this island component
		}
		ComponentTris.Append(&ConcatenatedIslandTids[StartIndex], Count);


		FAxisAlignedBox2f UVBounds = FAxisAlignedBox2f::Empty();
		for (int32 tid : ComponentTris)
		{
			if (UVOverlay->IsSetTriangle(tid))
			{
				FIndex3i UVTri = UVOverlay->GetTriangle(tid);
				FVector2f U = UVOverlay->GetElement(UVTri.A);
				FVector2f V = UVOverlay->GetElement(UVTri.B);
				FVector2f W = UVOverlay->GetElement(UVTri.C);
				UVBounds.Contain(U); UVBounds.Contain(V); UVBounds.Contain(W);
			}
		}


		bComponentSolved[k] = UVEditor.SetTriangleUVsFromFreeBoundaryConformal(ComponentTris, true);
		if (bComponentSolved[k])
		{
			UVEditor.ScaleUVAreaToBoundingBox(ComponentTris, UVBounds, true, true);
		}
	
		for (int32 Tid : ComponentTris)
		{
			FIndex3i TriVids = MeshToUnwrap.Attributes()->GetUVLayer(Targets[SelectionTargetIndex]->UVLayerIndex)->GetTriangle(Tid);
			for (int i = 0; i < 3; ++i)
			{
				VidSet.Add(TriVids[i]);
			}
		}
	}

	const TArray<int32> AllVids = VidSet.Array();

	Targets[SelectionTargetIndex]->UpdateAllFromAppliedCanonical(&AllVids, UUVEditorToolMeshInput::NONE_CHANGED_ARG, &ConcatenatedIslandTids);
	
	const FText TransactionName(LOCTEXT("ConformalUnwrapCompleteTransactionName", "Conformal Unwrap Islands"));
	EmitChangeAPI.EmitToolIndependentUnwrapCanonicalChange(Targets[SelectionTargetIndex],
		ChangeTracker.EndChange(), TransactionName);

	return true;
}

#undef LOCTEXT_NAMESPACE