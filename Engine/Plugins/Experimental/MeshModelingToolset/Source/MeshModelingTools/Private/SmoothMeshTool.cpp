// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmoothMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "SimpleDynamicMeshComponent.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "ToolSetupUtil.h"

// Smoothing operators
#include "SmoothingOps/IterativeSmoothingOp.h"
#include "SmoothingOps/CotanSmoothingOp.h"
//#include "SmoothingOps/MeanValueSmoothingOp.h"


#define LOCTEXT_NAMESPACE "USmoothMeshTool"


/*
 * ToolBuilder
 */


bool USmoothMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* USmoothMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	USmoothMeshTool* NewTool = NewObject<USmoothMeshTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);

	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}



/*
 * Tool
 */

void USmoothMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void USmoothMeshTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


void USmoothMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	ToolPropertyObjects.Add(this);

	// populate the SrcDynamicMesh with a conversion of the input mesh.
	FMeshDescriptionToDynamicMesh Converter;
	Converter.bPrintDebugMessages = false;
	Converter.Convert(ComponentTarget->GetMesh(), SrcDynamicMesh);

	// Initialize the preview mesh with a copy of the source mesh.
	{
		// Construct the preview object and set the material on it.
		Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
		Preview->Setup(this->TargetWorld, this); // Adds the actual functional tool in the Preview object

		FComponentMaterialSet MaterialSet;
		ComponentTarget->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
		Preview->PreviewMesh->UpdatePreview(&SrcDynamicMesh);
	}

	// show the preview mesh
	Preview->SetVisibility(true);

	// start the compute
	Preview->InvalidateResult();    // start compute

	bResultValid = false;

}


void USmoothMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
		ComponentTarget->SetOwnerVisibility(true);

	if (Preview != nullptr)
	{
		FDynamicMeshOpResult Result = Preview->Shutdown();
		if (ShutdownType == EToolShutdownType::Accept)
		{
			
			GetToolManager()->BeginUndoTransaction(LOCTEXT("SmoothMeshToolTransactionName", "Smooth Mesh"));

			FDynamicMesh3* DynamicMeshResult = Result.Mesh.Get();
			check(DynamicMeshResult != nullptr);

			ComponentTarget->CommitMesh([DynamicMeshResult](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FDynamicMeshToMeshDescription Converter;
				Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescription);
			});


			GetToolManager()->EndUndoTransaction();
		}
	}
}

void USmoothMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();
}

#if WITH_EDITOR
void USmoothMeshTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// one of the parameters changed.  Dirty the result for force a recompute.
	Preview->InvalidateResult();
	bResultValid = false;
}
#endif

void USmoothMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Preview->InvalidateResult();
	bResultValid = false;
}


void USmoothMeshTool::Tick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

void USmoothMeshTool::UpdateResult()
{
	if (bResultValid) 
	{
				return;
			}

	bResultValid = Preview->HaveValidResult();
}

bool USmoothMeshTool::HasAccept() const
{
	return true;
}

bool USmoothMeshTool::CanAccept() const
{
	return bResultValid;
}

TUniquePtr<FDynamicMeshOperator> USmoothMeshTool::MakeNewOperator()
{
	TUniquePtr<FSmoothingOpBase> MeshOp;
	switch (SmoothType)
	{
	default:
	case ESmoothMeshToolSmoothType::Iterative:
		MeshOp = MakeUnique<FIterativeSmoothingOp>(&SrcDynamicMesh, SmoothSpeed, SmoothIterations);
		break;

	case ESmoothMeshToolSmoothType::BiHarmonic_Cotan:
		MeshOp = MakeUnique<FCotanSmoothingOp>(&SrcDynamicMesh, SmoothSpeed, SmoothIterations);
		break;
/**
	case ESmoothMeshToolSmoothType::BiHarmonic_MVW:
		MeshOp = MakeShared<FMeanValueSmoothingOp>(&SrcDynamicMesh, SmoothSpeed, SmoothIterations);
		break;
*/
	}

	const FTransform XForm = ComponentTarget->GetWorldTransform();
	FTransform3d XForm3d(XForm);
	MeshOp->SetTransform(XForm3d);

	return MeshOp;

}




#undef LOCTEXT_NAMESPACE
