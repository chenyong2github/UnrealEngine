// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actions/UVIslandConformalUnwrapAction.h"

#include "Algo/Unique.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "Selection/UVToolSelection.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "PreviewMesh.h"

#include "Selection/UVToolSelectionAPI.h"

#include "ToolSetupUtil.h"
#include "ContextObjects/UVToolContextObjects.h"
#include "Parameterization/DynamicMeshUVEditor.h"
#include "Selections/MeshConnectedComponents.h"

#define LOCTEXT_NAMESPACE "UUVIslandConformalUnwrapAction"

using namespace UE::Geometry;

namespace UVIslandConformalUnwrapActionLocals
{
	bool GatherIslandTids(const FUVToolSelection& SelectionIn, 
		TArray<int32>& ConcatenatedIslandTidsOut, TArray<int32>& IslandStartIndicesOut, int32& MaxIslandSizeOut) {
		TRACE_CPUPROFILER_EVENT_SCOPE(UVIslandConformalUnwrapAction_GatherIslandTids);

		ConcatenatedIslandTidsOut.Reset();
		IslandStartIndicesOut.Reset();
		MaxIslandSizeOut = 0;

		FDynamicMesh3& AppliedMesh = *(SelectionIn.Target->AppliedCanonical);
		FDynamicMeshUVOverlay* UseOverlay = AppliedMesh.Attributes()->GetUVLayer(SelectionIn.Target->UVLayerIndex);

		FMeshConnectedComponents ConnectedComponents(&AppliedMesh);

		ConnectedComponents.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1) {
			return UseOverlay->AreTrianglesConnected(Triangle0, Triangle1);
			});

		int32 NumComponents = ConnectedComponents.Num();
		IslandStartIndicesOut.Add(0);
		for (int32 k = 0; k < NumComponents; ++k)
		{
			TSet<int32> ComponentTris = TSet<int32>(ConnectedComponents[k].Indices);
			TSet<int32> SelectedComponentTris = ComponentTris.Intersect(SelectionIn.SelectedIDs);
			if (SelectedComponentTris.Num() == ComponentTris.Num())
			{
				ConcatenatedIslandTidsOut.Append(SelectedComponentTris.Array());
				IslandStartIndicesOut.Add(IslandStartIndicesOut.Last(0) + SelectedComponentTris.Num());
				MaxIslandSizeOut = FMath::Max(MaxIslandSizeOut, SelectedComponentTris.Num());
			}
		}

		return true;
	}
}

bool UUVIslandConformalUnwrapAction::CanExecuteAction() const
{
	return SelectionAPI->HaveSelections() 
		&& SelectionAPI->GetSelectionsType() == FUVToolSelection::EType::Triangle;
}

bool UUVIslandConformalUnwrapAction::ExecuteAction()
{
	using namespace UVIslandConformalUnwrapActionLocals;

	TRACE_CPUPROFILER_EVENT_SCOPE(UVIslandConformalUnwrapAction_ApplyAction);

	for (const FUVToolSelection& Selection : SelectionAPI->GetSelections())
	{
		if (!ensure(Selection.Target.IsValid() && Selection.Target->IsValid()))
		{
			continue;
		}

		TArray<int32> ConcatenatedIslandTids;
		TArray<int32> IslandStartIndices;
		int32 MaxIslandSize;

		GatherIslandTids(Selection, ConcatenatedIslandTids, IslandStartIndices, MaxIslandSize);

		UUVEditorToolMeshInput* Target = Selection.Target.Get();

		FDynamicMesh3& MeshToUnwrap = *(Target->AppliedCanonical);
		FDynamicMeshUVOverlay* UVOverlay = MeshToUnwrap.Attributes()->GetUVLayer(Target->UVLayerIndex);
		FDynamicMeshUVEditor UVEditor(&MeshToUnwrap, Target->UVLayerIndex, true);

		TArray<int32> ComponentTris;
		ComponentTris.Reserve(MaxIslandSize);
		int32 NumComponents = IslandStartIndices.Num() - 1;
		TArray<bool> bComponentSolved;
		bComponentSolved.Init(false, NumComponents);
		int32 SuccessCount = 0;
		TSet<int32> VidSet;
		for (int32 k = 0; k < NumComponents; ++k)
		{
			ComponentTris.Reset(0);
			int32 StartIndex = IslandStartIndices[k];
			int32 Count = IslandStartIndices[k + 1] - StartIndex;
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
				FIndex3i TriVids = MeshToUnwrap.Attributes()->GetUVLayer(Target->UVLayerIndex)->GetTriangle(Tid);
				for (int i = 0; i < 3; ++i)
				{
					VidSet.Add(TriVids[i]);
				}
			}
		}

		if (VidSet.IsEmpty())
		{
			continue;
		}

		FDynamicMeshChangeTracker ChangeTracker(Target->UnwrapCanonical.Get());
		ChangeTracker.BeginChange();
		ChangeTracker.SaveTriangles(ConcatenatedIslandTids, true);

		const TArray<int32> AllVids = VidSet.Array();
		Target->UpdateAllFromAppliedCanonical(&AllVids, UUVEditorToolMeshInput::NONE_CHANGED_ARG, &ConcatenatedIslandTids);

		const FText TransactionName(LOCTEXT("ConformalUnwrapCompleteTransactionName", "Conformal Unwrap Islands"));
		EmitChangeAPI->EmitToolIndependentUnwrapCanonicalChange(Target,
			ChangeTracker.EndChange(), TransactionName);
	}

	// TODO: Should maybe give some kind of error message if there were no fully selected components?
	return true;
}

#undef LOCTEXT_NAMESPACE