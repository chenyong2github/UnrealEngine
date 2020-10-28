// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemeshMeshTool.h"
#include "ComponentSourceInterfaces.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "Util/ColorConstants.h"
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

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() == 1);

	auto* MeshComponent = Cast<UPrimitiveComponent>(Components[0]);
	check(MeshComponent != nullptr);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	ComponentTargets.Add(MakeComponentTarget(MeshComponent));

	NewTool->SetSelection(MoveTemp(ComponentTargets));
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
	SmoothingStrength = 0.25;
	RemeshIterations = 20;
	bDiscardAttributes = false;
	RemeshType = ERemeshType::Standard;
	SmoothingType = ERemeshSmoothingType::MeanValue;
	bPreserveSharpEdges = true;
	bShowWireframe = true;
	bShowGroupColors = false;

	TargetEdgeLength = 5.0;
	bFlips = true;
	bSplits = true;
	bCollapses = true;
	bReproject = true;
	bPreventNormalFlips = true;
	bUseTargetEdgeLength = false;
}

URemeshMeshTool::URemeshMeshTool(const FObjectInitializer&)
{
	BasicProperties = CreateDefaultSubobject<URemeshMeshToolProperties>(TEXT("RemeshProperties"));
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

	check(BasicProperties);
	BasicProperties->RestoreProperties(this);
	MeshStatisticsProperties = NewObject<UMeshStatisticsProperties>(this);

	check(ComponentTargets.Num() > 0);
	check(ComponentTargets[0]);
	FPrimitiveComponentTarget* ComponentTarget = ComponentTargets[0].Get();

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
	Preview->PreviewMesh->EnableWireframe(BasicProperties->bShowWireframe);

	BasicProperties->WatchProperty(BasicProperties->bShowGroupColors,
								   [this](bool bNewValue) { UpdateVisualization();});
	BasicProperties->WatchProperty(BasicProperties->bShowWireframe,
								   [this](bool bNewValue) { UpdateVisualization();});

	OriginalMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalMesh);

	Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);
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

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Retriangulate the selected Mesh. Use the Boundary Constraints to preserve mesh borders. Enable Discard Attributes to ignore UV/Normal Seams. "),
		EToolMessageLevel::UserNotification);
}

void URemeshMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	BasicProperties->SaveProperties(this);
	for (auto& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}
	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Result);
	}
}

void URemeshMeshTool::OnTick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

TUniquePtr<FDynamicMeshOperator> URemeshMeshTool::MakeNewOperator()
{
	TUniquePtr<FRemeshMeshOp> Op = MakeUnique<FRemeshMeshOp>();

	Op->RemeshType = BasicProperties->RemeshType;

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
	Op->MeshBoundaryConstraint = (EEdgeRefineFlags)BasicProperties->MeshBoundaryConstraint;
	Op->GroupBoundaryConstraint = (EEdgeRefineFlags)BasicProperties->GroupBoundaryConstraint;
	Op->MaterialBoundaryConstraint = (EEdgeRefineFlags)BasicProperties->MaterialBoundaryConstraint;
	Op->bPreventNormalFlips = BasicProperties->bPreventNormalFlips;
	Op->bReproject = BasicProperties->bReproject;
	Op->bSplits = BasicProperties->bSplits;
	Op->RemeshIterations = BasicProperties->RemeshIterations;
	Op->SmoothingStrength = BasicProperties->SmoothingStrength;
	Op->SmoothingType = BasicProperties->SmoothingType;

	check(ComponentTargets.Num() > 0);
	FTransform LocalToWorld = ComponentTargets[0]->GetWorldTransform();
	Op->SetTransform(LocalToWorld);

	Op->OriginalMesh = OriginalMesh;
	Op->OriginalMeshSpatial = OriginalMeshSpatial;

	Op->ProjectionTarget = nullptr;
	Op->ProjectionTargetSpatial = nullptr;

	return Op;
}

void URemeshMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FTransform Transform = ComponentTargets[0]->GetWorldTransform();

	FColor LineColor(255, 0, 0);
	const FDynamicMesh3* TargetMesh = Preview->PreviewMesh->GetPreviewDynamicMesh();
	if (TargetMesh && TargetMesh->HasAttributes())
	{
		float PDIScale = RenderAPI->GetCameraState().GetPDIScalingFactor();
		const FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->PrimaryUV();
		for (int eid : TargetMesh->EdgeIndicesItr())
		{
			if (UVOverlay->IsSeamEdge(eid))
			{
				FVector3d A, B;
				TargetMesh->GetEdgeV(eid, A, B);
				PDI->DrawLine(Transform.TransformPosition((FVector)A), Transform.TransformPosition((FVector)B),
					LineColor, 0, 2.0*PDIScale, 1.0f, true);
			}
		}
	}
}

void URemeshMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if ( Property )
	{
		if ( ( Property->GetFName() == GET_MEMBER_NAME_CHECKED(URemeshMeshToolProperties, bShowWireframe) ) ||
			 ( Property->GetFName() == GET_MEMBER_NAME_CHECKED(URemeshMeshToolProperties, bShowGroupColors) ) )
		{
			//UpdateVisualization();
		}
		else
		{
			Preview->InvalidateResult();
		}
	}
}

void URemeshMeshTool::UpdateVisualization()
{
	Preview->PreviewMesh->EnableWireframe(BasicProperties->bShowWireframe);
	FComponentMaterialSet MaterialSet;
	if (BasicProperties->bShowGroupColors)
	{
		MaterialSet.Materials = {ToolSetupUtil::GetSelectionMaterial(GetToolManager())};
		Preview->PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID)
		{
			return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
		},
		UPreviewMesh::ERenderUpdateMode::FastUpdate);
	}
	else
	{
		ComponentTargets[0]->GetMaterialSet(MaterialSet);
		Preview->PreviewMesh->ClearTriangleColorFunction(UPreviewMesh::ERenderUpdateMode::FastUpdate);
	}
	Preview->ConfigureMaterials(MaterialSet.Materials,
								ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
}

double URemeshMeshTool::CalculateTargetEdgeLength(int TargetTriCount)
{
	double TargetTriArea = InitialMeshArea / (double)TargetTriCount;
	double EdgeLen = TriangleUtil::EquilateralEdgeLengthForArea(TargetTriArea);
	return (double)FMath::RoundToInt(EdgeLen*100.0) / 100.0;
}

bool URemeshMeshTool::CanAccept() const
{
	return Super::CanAccept() && Preview->HaveValidResult();
}

void URemeshMeshTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("RemeshMeshToolTransactionName", "Remesh Mesh"));

	check(Result.Mesh.Get() != nullptr);
	ComponentTargets[0]->CommitMesh([&Result](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
	{
		FDynamicMeshToMeshDescription Converter;

		// full conversion if normal topology changed or faces were inverted
		Converter.Convert(Result.Mesh.Get(), *CommitParams.MeshDescription);
	});

	GetToolManager()->EndUndoTransaction();
}

#undef LOCTEXT_NAMESPACE
