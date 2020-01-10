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

	// copy material if there is one
	auto Material = ComponentTarget->GetMaterial(0);
	if (Material != nullptr)
	{
		DynamicMeshComponent->SetMaterial(0, Material);
	}

	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	OriginalMesh.Copy(*DynamicMeshComponent->GetMesh());

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	ToolPropertyObjects.Add(this);

	bResultValid = false;
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
			ComponentTarget->CommitMesh([=](FMeshDescription* MeshDescription)
			{
				DynamicMeshComponent->Bake(MeshDescription, true);
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
	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->PrimaryUV();
	for (int eid : TargetMesh->EdgeIndicesItr()) 
	{
		if (UVOverlay->IsSeamEdge(eid)) 
		{
			FVector3d A, B;
			TargetMesh->GetEdgeV(eid, A, B);
			PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
				LineColor, 0, 1.0, 1.0f, true);
		}
	}


	FColor LineColor2(255, 0, 0);
	for (int eid : TargetMesh->BoundaryEdgeIndicesItr()) 
	{
		FVector3d A, B;
		TargetMesh->GetEdgeV(eid, A, B);
		PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
			LineColor2, 0, 2.0, 1.0f, true);
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
	Merger.MergeSearchTolerance = this->Tolerance;
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

bool UWeldMeshEdgesTool::HasAccept() const
{
	return true;
}

bool UWeldMeshEdgesTool::CanAccept() const
{
	return true;
}




#undef LOCTEXT_NAMESPACE
