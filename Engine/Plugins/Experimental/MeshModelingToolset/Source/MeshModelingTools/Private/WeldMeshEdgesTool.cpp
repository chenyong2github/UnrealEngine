// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeldMeshEdgesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"

#include "SimpleDynamicMeshComponent.h"
#include "ModelingToolTargetUtil.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "Async/ParallelFor.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UWeldMeshEdgesTool"

/*
 * ToolBuilder
 */

USingleSelectionMeshEditingTool* UWeldMeshEdgesToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UWeldMeshEdgesTool>(SceneState.ToolManager);
}



/*
 * Tool
 */

UWeldMeshEdgesTool::UWeldMeshEdgesTool()
{
	Tolerance = FMergeCoincidentMeshEdges::DEFAULT_TOLERANCE;
	bOnlyUnique = false;
}

void UWeldMeshEdgesTool::Setup()
{
	UInteractiveTool::Setup();

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(UE::ToolTarget::GetTargetActor(Target));
	DynamicMeshComponent->SetupAttachment(UE::ToolTarget::GetTargetComponent(Target));
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform((FTransform)UE::ToolTarget::GetLocalToWorldTransform(Target));

	// transfer materials
	FComponentMaterialSet MaterialSet = UE::ToolTarget::GetMaterialSet(Target);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
	DynamicMeshComponent->SetMesh(UE::ToolTarget::GetDynamicMeshCopy(Target));
	DynamicMeshComponent->ProcessMesh([&](const FDynamicMesh3& ReadMesh) { OriginalMesh = ReadMesh; });

	// hide input StaticMeshComponent
	UE::ToolTarget::HideSourceObject(Target);

	// initialize our properties
	ToolPropertyObjects.Add(this);

	bResultValid = false;

	SetToolDisplayName(LOCTEXT("ToolName", "Weld Edges"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("WeldMeshEdgesToolDescription", "Weld overlapping/identical border edges of the selected Mesh, by merging the vertices."),
		EToolMessageLevel::UserNotification);
}


void UWeldMeshEdgesTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		UE::ToolTarget::ShowSourceObject(Target);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("WeldMeshEdgesToolTransactionName", "Remesh Mesh"));
			DynamicMeshComponent->ProcessMesh([&](const FDynamicMesh3& CurMesh)
			{
				UE::ToolTarget::CommitDynamicMeshUpdate(Target, CurMesh, true);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}


void UWeldMeshEdgesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FTransform Transform = (FTransform)UE::ToolTarget::GetLocalToWorldTransform(Target);

	FColor LineColor(200, 200, 200);
	float PDIScale = RenderAPI->GetCameraState().GetPDIScalingFactor();
	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->PrimaryUV();
	for (int eid : TargetMesh->EdgeIndicesItr()) 
	{
		if (UVOverlay->IsSeamEdge(eid)) 
		{
			FVector3d A, B;
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine(Transform.TransformPosition((FVector)A), Transform.TransformPosition((FVector)B),
				LineColor, 0, 1.0f*PDIScale, 1.0f, true);
		}
	}


	FColor LineColor2(255, 0, 0);
	for (int eid : TargetMesh->BoundaryEdgeIndicesItr()) 
	{
		FVector3d A, B;
		TargetMesh->GetEdgeV(eid, A, B);
		PDI->DrawLine(Transform.TransformPosition((FVector)A), Transform.TransformPosition((FVector)B),
			LineColor2, 0, 2.0f*PDIScale, 1.0f, true);
	}

}

#if WITH_EDITOR
void UWeldMeshEdgesTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	bResultValid = false;
}
#endif

void UWeldMeshEdgesTool::UpdateResult()
{
	if (bResultValid) 
	{
		return;
	}

	ComputeMesh = OriginalMesh;

	FMergeCoincidentMeshEdges Merger(&ComputeMesh);
	Merger.MergeVertexTolerance = this->Tolerance;
	Merger.MergeSearchTolerance = 2*Merger.MergeVertexTolerance;
	Merger.OnlyUniquePairs = this->bOnlyUnique;
	FDynamicMesh3* SetMesh = &ComputeMesh;
		
	if (Merger.Apply() == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("WeldMeshEdgesTool : Merger.Apply() returned false"));
		SetMesh = &OriginalMesh;
	}
	else if (ComputeMesh.CheckValidity(true, EValidityCheckFailMode::ReturnOnly) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("WeldMeshEdgesTool : returned mesh is invalid"));
		SetMesh = &OriginalMesh;
	}

	DynamicMeshComponent->EditMesh([SetMesh](FDynamicMesh3& EditMesh)
	{
		EditMesh = *SetMesh;
	});

	GetToolManager()->PostInvalidation();
	bResultValid = true;
}





#undef LOCTEXT_NAMESPACE
