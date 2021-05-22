// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeldMeshEdgesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"

#include "SimpleDynamicMeshComponent.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "Async/ParallelFor.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

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
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(TargetComponent->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(TargetComponent->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(TargetComponent->GetWorldTransform());

	// transfer materials
	FComponentMaterialSet MaterialSet;
	Cast<IMaterialProvider>(Target)->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(Cast<IMeshDescriptionProvider>(Target)->GetMeshDescription());
	OriginalMesh.Copy(*DynamicMeshComponent->GetMesh());

	// hide input StaticMeshComponent
	TargetComponent->SetOwnerVisibility(false);

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
		Cast<IPrimitiveComponentBackedTarget>(Target)->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("WeldMeshEdgesToolTransactionName", "Remesh Mesh"));
			Cast<IMeshDescriptionCommitter>(Target)->CommitMeshDescription([this](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
			{
				DynamicMeshComponent->Bake(CommitParams.MeshDescriptionOut, true);
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
	FTransform Transform = Cast<IPrimitiveComponentBackedTarget>(Target)->GetWorldTransform(); //Actor->GetTransform();

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

	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	TargetMesh->Copy(OriginalMesh);

	FMergeCoincidentMeshEdges Merger(TargetMesh);
	Merger.MergeVertexTolerance = this->Tolerance;
	Merger.MergeSearchTolerance = 2*Merger.MergeVertexTolerance;
	Merger.OnlyUniquePairs = this->bOnlyUnique;
		
	if (Merger.Apply() == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("WeldMeshEdgesTool : Merger.Apply() returned false"));
		TargetMesh->Copy(OriginalMesh);
	}

	if (TargetMesh->CheckValidity(true, EValidityCheckFailMode::ReturnOnly) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("WeldMeshEdgesTool : returned mesh is invalid"));
		TargetMesh->Copy(OriginalMesh);
	}

	DynamicMeshComponent->NotifyMeshUpdated();
	GetToolManager()->PostInvalidation();

	bResultValid = true;
}





#undef LOCTEXT_NAMESPACE
