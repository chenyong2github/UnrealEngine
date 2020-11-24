// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimplifyMeshTool.h"
#include "InteractiveToolManager.h"
#include "Properties/RemeshProperties.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"
#include "Util/ColorConstants.h"

#include "DynamicMesh3.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface

//#include "ProfilingDebugging/ScopedTimers.h" // enable this to use the timer.
#include "Modules/ModuleManager.h"


#include "IMeshReductionManagerModule.h"
#include "IMeshReductionInterfaces.h"


#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

#define LOCTEXT_NAMESPACE "USimplifyMeshTool"

DEFINE_LOG_CATEGORY_STATIC(LogMeshSimplification, Log, All);

/*
 * ToolBuilder
 */
bool USimplifyMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* USimplifyMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	USimplifyMeshTool* NewTool = NewObject<USimplifyMeshTool>(SceneState.ToolManager);

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
USimplifyMeshToolProperties::USimplifyMeshToolProperties()
{
	SimplifierType = ESimplifyType::QEM;
	TargetMode = ESimplifyTargetType::Percentage;
	TargetPercentage = 50;
	TargetCount = 1000;
	TargetEdgeLength = 5.0;
	bReproject = false;
	bPreventNormalFlips = true;
	bDiscardAttributes = false;
	bShowWireframe = true;
	bShowGroupColors = false;
	GroupBoundaryConstraint = EGroupBoundaryConstraint::Ignore;
	MaterialBoundaryConstraint = EMaterialBoundaryConstraint::Ignore;
}

void USimplifyMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void USimplifyMeshTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

void USimplifyMeshTool::Setup()
{
	UInteractiveTool::Setup();


	// hide component and create + show preview
	ComponentTarget->SetOwnerVisibility(false);
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials( MaterialSet.Materials,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);

	{
		// if in editor, create progress indicator dialog because building mesh copies can be slow (for very large meshes)
		// this is especially needed because of the copy we make of the meshdescription; for Reasons, copying meshdescription is pretty slow
#if WITH_EDITOR
		static const FText SlowTaskText = LOCTEXT("SimplifyMeshInit", "Building mesh simplification data...");

		FScopedSlowTask SlowTask(3.0f, SlowTaskText);
		SlowTask.MakeDialog();

		// Declare progress shortcut lambdas
		auto EnterProgressFrame = [&SlowTask](int Progress)
	{
			SlowTask.EnterProgressFrame((float)Progress);
		};
#else
		auto EnterProgressFrame = [](int Progress) {};
#endif
		OriginalMeshDescription = MakeShared<FMeshDescription>(*ComponentTarget->GetMesh());
		EnterProgressFrame(1);
		OriginalMesh = MakeShared<FDynamicMesh3>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(ComponentTarget->GetMesh(), *OriginalMesh);
		EnterProgressFrame(2);
		OriginalMeshSpatial = MakeShared<FDynamicMeshAABBTree3>(OriginalMesh.Get(), true);
	}

	Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);
	Preview->PreviewMesh->UpdatePreview(OriginalMesh.Get());

	// initialize our properties
	SimplifyProperties = NewObject<USimplifyMeshToolProperties>(this);
	SimplifyProperties->RestoreProperties(this);
	AddToolPropertySource(SimplifyProperties);

	SimplifyProperties->WatchProperty(SimplifyProperties->bShowGroupColors,
									  [this](bool bNewValue) { UpdateVisualization(); });
	SimplifyProperties->WatchProperty(SimplifyProperties->bShowWireframe,
									  [this](bool bNewValue) { UpdateVisualization(); });

	MeshStatisticsProperties = NewObject<UMeshStatisticsProperties>(this);
	AddToolPropertySource(MeshStatisticsProperties);

	Preview->OnMeshUpdated.AddLambda([this](UMeshOpPreviewWithBackgroundCompute* Compute)
	{
		MeshStatisticsProperties->Update(*Compute->PreviewMesh->GetPreviewDynamicMesh());
	});

	UpdateVisualization();
	Preview->InvalidateResult();

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Reduce the number of triangles in the selected Mesh using various strategies."),
		EToolMessageLevel::UserNotification);
}


bool USimplifyMeshTool::CanAccept() const
{
	return Super::CanAccept() && Preview->HaveValidResult();
}


void USimplifyMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	SimplifyProperties->SaveProperties(this);
	ComponentTarget->SetOwnerVisibility(true);
	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Result);
	}
}


void USimplifyMeshTool::OnTick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

TUniquePtr<FDynamicMeshOperator> USimplifyMeshTool::MakeNewOperator()
{
	TUniquePtr<FSimplifyMeshOp> Op = MakeUnique<FSimplifyMeshOp>();

	Op->bDiscardAttributes = SimplifyProperties->bDiscardAttributes;
	Op->bPreventNormalFlips = SimplifyProperties->bPreventNormalFlips;
	Op->bPreserveSharpEdges = SimplifyProperties->bPreserveSharpEdges;
	Op->bAllowSeamCollapse = !SimplifyProperties->bPreserveSharpEdges;
	Op->bReproject = SimplifyProperties->bReproject;
	Op->SimplifierType = SimplifyProperties->SimplifierType;
	Op->TargetCount = SimplifyProperties->TargetCount;
	Op->TargetEdgeLength = SimplifyProperties->TargetEdgeLength;
	Op->TargetMode = SimplifyProperties->TargetMode;
	Op->TargetPercentage = SimplifyProperties->TargetPercentage;
	Op->MeshBoundaryConstraint = (EEdgeRefineFlags)SimplifyProperties->MeshBoundaryConstraint;
	Op->GroupBoundaryConstraint = (EEdgeRefineFlags)SimplifyProperties->GroupBoundaryConstraint;
	Op->MaterialBoundaryConstraint = (EEdgeRefineFlags)SimplifyProperties->MaterialBoundaryConstraint;
	FTransform LocalToWorld = ComponentTarget->GetWorldTransform();
	Op->SetTransform(LocalToWorld);

	Op->OriginalMeshDescription = OriginalMeshDescription;
	Op->OriginalMesh = OriginalMesh;
	Op->OriginalMeshSpatial = OriginalMeshSpatial;

	IMeshReductionManagerModule& MeshReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface");
	Op->MeshReduction = MeshReductionModule.GetStaticMeshReductionInterface();

	return Op;
}

void USimplifyMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FTransform Transform = ComponentTarget->GetWorldTransform(); //Actor->GetTransform();

	FColor LineColor(255, 0, 0);
	const FDynamicMesh3* TargetMesh = Preview->PreviewMesh->GetPreviewDynamicMesh();
	if (TargetMesh->HasAttributes())
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

void USimplifyMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if ( Property )
	{
		if ( ( Property->GetFName() == GET_MEMBER_NAME_CHECKED(USimplifyMeshToolProperties, bShowWireframe) ) ||
			 ( Property->GetFName() == GET_MEMBER_NAME_CHECKED(USimplifyMeshToolProperties, bShowGroupColors) ) )
		{
			UpdateVisualization();
		}
		else
		{
			Preview->InvalidateResult();
		}
	}
}

void USimplifyMeshTool::UpdateVisualization()
{
	Preview->PreviewMesh->EnableWireframe(SimplifyProperties->bShowWireframe);
	FComponentMaterialSet MaterialSet;
	if (SimplifyProperties->bShowGroupColors)
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
		ComponentTarget->GetMaterialSet(MaterialSet);
		Preview->PreviewMesh->ClearTriangleColorFunction(UPreviewMesh::ERenderUpdateMode::FastUpdate);
	}
	Preview->ConfigureMaterials(MaterialSet.Materials,
								ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
}

void USimplifyMeshTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("SimplifyMeshToolTransactionName", "Simplify Mesh"));

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
