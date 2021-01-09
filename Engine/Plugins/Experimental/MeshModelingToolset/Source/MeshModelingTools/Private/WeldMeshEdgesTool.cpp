// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeldMeshEdgesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "Operations/MergeCoincidentMeshEdges.h"

#include "SimpleDynamicMeshComponent.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "Async/ParallelFor.h"

#define LOCTEXT_NAMESPACE "UWeldMeshEdgesTool"


/*
 * ToolBuilder
 */


bool UWeldMeshEdgesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UWeldMeshEdgesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UWeldMeshEdgesTool* NewTool = NewObject<UWeldMeshEdgesTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);

	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	return NewTool;
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
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());

	// transfer materials
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	OriginalMesh.Copy(*DynamicMeshComponent->GetMesh());

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	ToolPropertyObjects.Add(this);

	bResultValid = false;

	GetToolManager()->DisplayMessage(
		LOCTEXT("WeldMeshEdgesToolDescription", "Weld overlapping/identical border edges of the selected Mesh, by merging the vertices."),
		EToolMessageLevel::UserNotification);
}


void UWeldMeshEdgesTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("WeldMeshEdgesToolTransactionName", "Remesh Mesh"));
			ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, true);
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
	FTransform Transform = ComponentTarget->GetWorldTransform(); //Actor->GetTransform();

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
