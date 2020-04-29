// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmoothMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "MeshTransforms.h"
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

	// compute area of the input mesh and compute normalization scaling factor
	FVector2d VolArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(SrcDynamicMesh);
	double AreaScale = FMathd::Max(0.01, 6.0 / FMathd::Sqrt(VolArea.Y) );	// 6.0 is a bit arbitrary here...surface area of unit box

	// translate to origin and then apply inverse of scale
	FAxisAlignedBox3d Bounds = SrcDynamicMesh.GetCachedBounds();
	SrcTranslate = Bounds.Center();
	MeshTransforms::Translate(SrcDynamicMesh, -SrcTranslate);
	SrcScale = AreaScale;
	MeshTransforms::Scale(SrcDynamicMesh, (1.0/SrcScale)*FVector3d::One(), FVector3d::Zero() );

	// apply that transform to target transform so that visible mesh stays in the same spot
	OverrideTransform = ComponentTarget->GetWorldTransform();
	FVector TranslateDelta = OverrideTransform.TransformVector((FVector)SrcTranslate);
	FVector CurScale = OverrideTransform.GetScale3D();
	OverrideTransform.AddToTranslation(TranslateDelta);
	CurScale.X *= (float)SrcScale;
	CurScale.Y *= (float)SrcScale;
	CurScale.Z *= (float)SrcScale;
	OverrideTransform.SetScale3D(CurScale);

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
		Preview->PreviewMesh->SetTransform(OverrideTransform);
		Preview->PreviewMesh->UpdatePreview(&SrcDynamicMesh);
	}

	// calculate normals
	BaseNormals = MakeShared<FMeshNormals>(&SrcDynamicMesh);
	BaseNormals->ComputeVertexNormals();

	// show the preview mesh
	Preview->SetVisibility(true);

	SmoothProperties = NewObject<USmoothMeshToolProperties>(this);
	AddToolPropertySource(SmoothProperties);
	SmoothProperties->RestoreProperties(this);
	SmoothProperties->WatchProperty(SmoothProperties->SmoothingType,
		[&](ESmoothMeshToolSmoothType) { UpdateVisiblePropertySets(); InvalidateResult();  });

	IterativeProperties = NewObject<UIterativeSmoothProperties>(this);
	AddToolPropertySource(IterativeProperties);
	IterativeProperties->RestoreProperties(this);
	SetToolPropertySourceEnabled(IterativeProperties, false);
	IterativeProperties->GetOnModified().AddLambda([this](UObject*, FProperty*) { InvalidateResult(); });

	DiffusionProperties = NewObject<UDiffusionSmoothProperties>(this);
	AddToolPropertySource(DiffusionProperties);
	DiffusionProperties->RestoreProperties(this);
	SetToolPropertySourceEnabled(DiffusionProperties, false);
	DiffusionProperties->GetOnModified().AddLambda([this](UObject*, FProperty*) { InvalidateResult(); });

	ImplicitProperties = NewObject<UImplicitSmoothProperties>(this);
	AddToolPropertySource(ImplicitProperties);
	ImplicitProperties->RestoreProperties(this);
	SetToolPropertySourceEnabled(ImplicitProperties, false);
	ImplicitProperties->GetOnModified().AddLambda([this](UObject*, FProperty*) { InvalidateResult(); });

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

			MeshTransforms::Scale(*DynamicMeshResult, FVector3d(SrcScale, SrcScale, SrcScale), FVector3d::Zero());
			MeshTransforms::Translate(*DynamicMeshResult, SrcTranslate);

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
	Options.BaseNormals = this->BaseNormals;

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
		{	
		Options.SmoothAlpha = ImplicitProperties->SmoothSpeed;
		double NonlinearT = FMathd::Pow(ImplicitProperties->Smoothness, 2.0);
		// this is an empirically-determined hack that seems to work OK to normalize the smoothing result for variable vertex count...
		double ScaledPower = (NonlinearT/50.0) * SrcDynamicMesh.VertexCount();
		Options.SmoothPower = ScaledPower;
		Options.bUniform = ImplicitProperties->bPreserveUVs == false;
		Options.bUseImplicit = true;
		Options.NormalOffset = ImplicitProperties->VolumeCorrection;
		MeshOp = MakeUnique<FCotanSmoothingOp>(&SrcDynamicMesh, Options);
		}
		break;
	}

	FTransform3d XForm3d(OverrideTransform);
	MeshOp->SetTransform(XForm3d);

	return MeshOp;

}




#undef LOCTEXT_NAMESPACE
