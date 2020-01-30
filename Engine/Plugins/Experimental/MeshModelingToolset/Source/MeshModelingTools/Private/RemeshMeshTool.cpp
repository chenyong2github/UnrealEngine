// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemeshMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface

#define LOCTEXT_NAMESPACE "URemeshMeshTool"


/*
 * ToolBuilder
 */


bool URemeshMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* URemeshMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	URemeshMeshTool* NewTool = NewObject<URemeshMeshTool>(SceneState.ToolManager);

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

URemeshMeshToolProperties::URemeshMeshToolProperties()
{
	TargetTriangleCount = 5000;
	SmoothingSpeed = 0.25;
	RemeshIterations = 20;
	bDiscardAttributes = false;
	SmoothingType = ERemeshSmoothingType::MeanValue;
	bPreserveSharpEdges = true;

	TargetEdgeLength = 5.0;
	bFlips = true;
	bSplits = true;
	bCollapses = true;
	bReproject = true;
	bPreventNormalFlips = true;
	bUseTargetEdgeLength = false;
}



URemeshMeshTool::URemeshMeshTool()
{
}


void URemeshMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void URemeshMeshTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


void URemeshMeshTool::Setup()
{
	UInteractiveTool::Setup();

	BasicProperties = NewObject<URemeshMeshToolProperties>(this);
	MeshStatisticsProperties = NewObject<UMeshStatisticsProperties>(this);

	// hide component and create + show preview
	ComponentTarget->SetOwnerVisibility(false);
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	Preview->OnMeshUpdated.AddLambda([this](UMeshOpPreviewWithBackgroundCompute* Compute)
	{
		MeshStatisticsProperties->Update(*Compute->PreviewMesh->GetPreviewDynamicMesh());
	});
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials( MaterialSet.Materials,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);
	Preview->PreviewMesh->EnableWireframe(true);

	OriginalMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.bPrintDebugMessages = true;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalMesh);

	Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	Preview->PreviewMesh->UpdatePreview(OriginalMesh.Get());

	OriginalMeshSpatial = MakeShared<FDynamicMeshAABBTree3>(OriginalMesh.Get(), true);

	// calculate initial mesh area (no utility fn yet)
	// TODO: will need to change to account for component transform's Scale3D
	InitialMeshArea = 0;
	for (int tid : OriginalMesh->TriangleIndicesItr())
	{
		InitialMeshArea += OriginalMesh->GetTriArea(tid);
	}
	
	// set properties defaults
	
	// arbitrary threshold of 5000 tris seems reasonable? 
	BasicProperties->TargetTriangleCount = (OriginalMesh->TriangleCount() < 5000) ? 5000 : OriginalMesh->TriangleCount();
	BasicProperties->TargetEdgeLength = CalculateTargetEdgeLength(BasicProperties->TargetTriangleCount);

	// add properties to GUI
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(MeshStatisticsProperties);

	Preview->InvalidateResult();
}


void URemeshMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	ComponentTarget->SetOwnerVisibility(true);
	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Result);
	}
}


void URemeshMeshTool::Tick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}


TUniquePtr<FDynamicMeshOperator> URemeshMeshTool::MakeNewOperator()
{
	TUniquePtr<FRemeshMeshOp> Op = MakeUnique<FRemeshMeshOp>();

	if (!BasicProperties->bUseTargetEdgeLength)
	{
		Op->TargetEdgeLength = CalculateTargetEdgeLength(BasicProperties->TargetTriangleCount);
	}
	else
	{
		Op->TargetEdgeLength = BasicProperties->TargetEdgeLength;
	}

	Op->bCollapses = BasicProperties->bCollapses;
	Op->bDiscardAttributes = BasicProperties->bDiscardAttributes;
	Op->bFlips = BasicProperties->bFlips;
	Op->bPreserveSharpEdges = BasicProperties->bPreserveSharpEdges;
	Op->bPreventNormalFlips = BasicProperties->bPreventNormalFlips;
	Op->bReproject = BasicProperties->bReproject;
	Op->bSplits = BasicProperties->bSplits;
	Op->RemeshIterations = BasicProperties->RemeshIterations;
	Op->SmoothingSpeed = BasicProperties->SmoothingSpeed;
	Op->SmoothingType = BasicProperties->SmoothingType;
	Op->TargetTriangleCount = BasicProperties->TargetTriangleCount;

	FTransform LocalToWorld = ComponentTarget->GetWorldTransform();
	Op->SetTransform(LocalToWorld);

	Op->OriginalMesh = OriginalMesh;
	Op->OriginalMeshSpatial = OriginalMeshSpatial;

	return Op;
}


void URemeshMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FTransform Transform = ComponentTarget->GetWorldTransform();

	FColor LineColor(255, 0, 0);
	const FDynamicMesh3* TargetMesh = Preview->PreviewMesh->GetPreviewDynamicMesh();
	if (TargetMesh && TargetMesh->HasAttributes())
	{
		const FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->PrimaryUV();
		for (int eid : TargetMesh->EdgeIndicesItr()) 
		{
			if (UVOverlay->IsSeamEdge(eid)) 
			{
				FVector3d A, B;
				TargetMesh->GetEdgeV(eid, A, B);
				PDI->DrawLine(Transform.TransformPosition((FVector)A), Transform.TransformPosition((FVector)B),
					LineColor, 0, 2.0, 1.0f, true);
			}
		}
	}
}


void URemeshMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Preview->InvalidateResult();
}


double URemeshMeshTool::CalculateTargetEdgeLength(int TargetTriCount)
{
	double TargetTriArea = InitialMeshArea / (double)TargetTriCount;
	double EdgeLen = TriangleUtil::EquilateralEdgeLengthForArea(TargetTriArea);
	return (double)FMath::RoundToInt(EdgeLen*100.0) / 100.0;
}

bool URemeshMeshTool::HasAccept() const
{
	return true;
}

bool URemeshMeshTool::CanAccept() const
{
	return Preview->HaveValidResult();
}


void URemeshMeshTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("RemeshMeshToolTransactionName", "Remesh Mesh"));

	check(Result.Mesh.Get() != nullptr);
	ComponentTarget->CommitMesh([&Result](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
	{
		FDynamicMeshToMeshDescription Converter;

		// full conversion if normal topology changed or faces were inverted
		Converter.Convert(Result.Mesh.Get(), *CommitParams.MeshDescription);
	});

	GetToolManager()->EndUndoTransaction();
}



#undef LOCTEXT_NAMESPACE
