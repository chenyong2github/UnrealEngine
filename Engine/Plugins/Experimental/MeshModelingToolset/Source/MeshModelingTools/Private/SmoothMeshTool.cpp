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
	Converter.Convert(ComponentTarget->GetMesh(), SrcDynamicMesh);

	// Initialize the preview mesh with a copy of the source mesh.
	{
		// Construct the preview object and set the material on it.
		Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
		Preview->Setup(this->TargetWorld, this); // Adds the actual functional tool in the Preview object
		Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

		FComponentMaterialSet MaterialSet;
		ComponentTarget->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);
		Preview->SetWorkingMaterialDelay(0.75);
		Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
		Preview->PreviewMesh->UpdatePreview(&SrcDynamicMesh);
	}

	// show the preview mesh
	Preview->SetVisibility(true);

	SmoothProperties = NewObject<USmoothMeshToolProperties>(this);
	AddToolPropertySource(SmoothProperties);
	SmoothProperties->RestoreProperties(this);
	SmoothProperties->WatchProperty(SmoothProperties->SmoothingType,
		[&](ESmoothMeshToolSmoothType) { UpdateVisiblePropertySets(); InvalidateResult();  });
	//SmoothProperties->WatchProperty(SmoothProperties->bPreserveUVs,
	//	[&](bool) { InvalidateResult(); });

	IterativeProperties = NewObject<UIterativeSmoothProperties>(this);
	AddToolPropertySource(IterativeProperties);
	IterativeProperties->RestoreProperties(this);
	SetToolPropertySourceEnabled(IterativeProperties, false);
	IterativeProperties->GetOnModified().AddLambda([this](UObject*, FProperty*) { InvalidateResult(); });
	//IterativeProperties->WatchProperty(IterativeProperties->SmoothingPerStep,[&](float) { InvalidateResult(); } );
	//IterativeProperties->WatchProperty(IterativeProperties->Steps, [&](int) { InvalidateResult(); });
	//IterativeProperties->WatchProperty(IterativeProperties->bSmoothBoundary, [&](int) { InvalidateResult(); });

	DiffusionProperties = NewObject<UDiffusionSmoothProperties>(this);
	AddToolPropertySource(DiffusionProperties);
	DiffusionProperties->RestoreProperties(this);
	SetToolPropertySourceEnabled(DiffusionProperties, false);
	DiffusionProperties->GetOnModified().AddLambda([this](UObject*, FProperty*) { InvalidateResult(); });
	//DiffusionProperties->WatchProperty(DiffusionProperties->SmoothingPerStep, [&](float) { InvalidateResult(); });
	//DiffusionProperties->WatchProperty(DiffusionProperties->Steps, [&](int) { InvalidateResult(); });

	ImplicitProperties = NewObject<UImplicitSmoothProperties>(this);
	AddToolPropertySource(ImplicitProperties);
	ImplicitProperties->RestoreProperties(this);
	SetToolPropertySourceEnabled(ImplicitProperties, false);
	ImplicitProperties->GetOnModified().AddLambda([this](UObject*, FProperty*) { InvalidateResult(); });
	//ImplicitProperties->WatchProperty(ImplicitProperties->SmoothSpeed, [&](float) { InvalidateResult(); });
	//ImplicitProperties->WatchProperty(ImplicitProperties->Smoothness, [&](float) { InvalidateResult(); });

	// start the compute
	InvalidateResult();

	GetToolManager()->DisplayMessage(
		LOCTEXT("StartSmoothToolMessage", "Smooths the mesh vertex positions using various smoothing methods."),
		EToolMessageLevel::UserNotification);
}


void USmoothMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	SmoothProperties->SaveProperties(this);
	IterativeProperties->SaveProperties(this);
	DiffusionProperties->SaveProperties(this);
	ImplicitProperties->SaveProperties(this);

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



void USmoothMeshTool::OnTick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

void USmoothMeshTool::InvalidateResult()
{
	Preview->InvalidateResult();
	bResultValid = false;
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


void USmoothMeshTool::UpdateVisiblePropertySets()
{
	SetToolPropertySourceEnabled(IterativeProperties, false);
	SetToolPropertySourceEnabled(DiffusionProperties, false);
	SetToolPropertySourceEnabled(ImplicitProperties, false);

	switch (SmoothProperties->SmoothingType)
	{
	case ESmoothMeshToolSmoothType::Iterative:
		SetToolPropertySourceEnabled(IterativeProperties, true);
		break;
	case ESmoothMeshToolSmoothType::Diffusion:
		SetToolPropertySourceEnabled(DiffusionProperties, true);
		break;
	case ESmoothMeshToolSmoothType::Implicit:
		SetToolPropertySourceEnabled(ImplicitProperties, true);
		break;
	}
}


TUniquePtr<FDynamicMeshOperator> USmoothMeshTool::MakeNewOperator()
{
	TUniquePtr<FSmoothingOpBase> MeshOp;
	
	FSmoothingOpBase::FOptions Options;

	switch (SmoothProperties->SmoothingType)
	{
	default:
	case ESmoothMeshToolSmoothType::Iterative:
		Options.SmoothAlpha = IterativeProperties->SmoothingPerStep;
		Options.Iterations = IterativeProperties->Steps;
		Options.bSmoothBoundary = IterativeProperties->bSmoothBoundary;
		Options.bUniform = true;
		Options.bUseImplicit = false;
		MeshOp = MakeUnique<FIterativeSmoothingOp>(&SrcDynamicMesh, Options);
		break;

	case ESmoothMeshToolSmoothType::Diffusion:
		Options.SmoothAlpha = DiffusionProperties->SmoothingPerStep;
		Options.Iterations = DiffusionProperties->Steps;
		Options.bUniform = DiffusionProperties->bPreserveUVs == false;
		Options.bUseImplicit = true;
		MeshOp = MakeUnique<FIterativeSmoothingOp>(&SrcDynamicMesh, Options);
		break;

	case ESmoothMeshToolSmoothType::Implicit:
		Options.SmoothAlpha = ImplicitProperties->SmoothSpeed;
		Options.SmoothPower = (ImplicitProperties->Smoothness >= 100.0) ? FMathf::MaxReal : ImplicitProperties->Smoothness;
		Options.bUniform = ImplicitProperties->bPreserveUVs == false;
		Options.bUseImplicit = true;
		MeshOp = MakeUnique<FCotanSmoothingOp>(&SrcDynamicMesh, Options);
		break;
	}

	const FTransform XForm = ComponentTarget->GetWorldTransform();
	FTransform3d XForm3d(XForm);
	MeshOp->SetTransform(XForm3d);

	return MeshOp;

}




#undef LOCTEXT_NAMESPACE
