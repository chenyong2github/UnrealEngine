// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SimplifyMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

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
	Preview->ConfigureMaterials(
		ToolSetupUtil::GetDefaultMaterial(GetToolManager(), ComponentTarget->GetMaterial(0)),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);
	Preview->PreviewMesh->EnableWireframe(true);

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
		Converter.bPrintDebugMessages = true;
		Converter.Convert(ComponentTarget->GetMesh(), *OriginalMesh);
		EnterProgressFrame(2);
		OriginalMeshSpatial = MakeShared<FDynamicMeshAABBTree3>(OriginalMesh.Get(), true);
	}

	Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	Preview->PreviewMesh->UpdatePreview(OriginalMesh.Get());

	// initialize our properties
	SimplifyProperties = NewObject<USimplifyMeshToolProperties>(this);
	AddToolPropertySource(SimplifyProperties);

	MeshStatisticsProperties = NewObject<UMeshStatisticsProperties>(this);
	AddToolPropertySource(MeshStatisticsProperties);

	Preview->OnMeshUpdated.AddLambda([this](UMeshOpPreviewWithBackgroundCompute* Compute)
	{
		MeshStatisticsProperties->Update(*Compute->PreviewMesh->GetPreviewDynamicMesh());
	});

	Preview->InvalidateResult();
}


void USimplifyMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	ComponentTarget->SetOwnerVisibility(true);
	TUniquePtr<FDynamicMeshOpResult> Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(*Result);
	}
}


void USimplifyMeshTool::Tick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

TSharedPtr<FDynamicMeshOperator> USimplifyMeshTool::MakeNewOperator()
{
	TSharedPtr<FSimplifyMeshOp> Op = MakeShared<FSimplifyMeshOp>();

	Op->bDiscardAttributes = SimplifyProperties->bDiscardAttributes;
	Op->bPreventNormalFlips = SimplifyProperties->bPreventNormalFlips;
	Op->bReproject = SimplifyProperties->bReproject;
	Op->SimplifierType = SimplifyProperties->SimplifierType;
	Op->TargetCount = SimplifyProperties->TargetCount;
	Op->TargetEdgeLength = SimplifyProperties->TargetEdgeLength;
	Op->TargetMode = SimplifyProperties->TargetMode;
	Op->TargetPercentage = SimplifyProperties->TargetPercentage;

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
		const FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->PrimaryUV();
		for (int eid : TargetMesh->EdgeIndicesItr()) 
		{
			if (UVOverlay->IsSeamEdge(eid)) 
			{
				FVector3d A, B;
				TargetMesh->GetEdgeV(eid, A, B);
				PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
					LineColor, 0, 2.0, 1.0f, true);
			}
		}
	}
}


void USimplifyMeshTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	Preview->InvalidateResult();
}

bool USimplifyMeshTool::HasAccept() const
{
	return true;
}

bool USimplifyMeshTool::CanAccept() const
{
	return true;
}

void USimplifyMeshTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("SimplifyMeshToolTransactionName", "Simplify Mesh"));

	check(Result.Mesh.Get() != nullptr);
	ComponentTarget->CommitMesh([&Result](FMeshDescription* MeshDescription)
	{
		FDynamicMeshToMeshDescription Converter;

		// full conversion if normal topology changed or faces were inverted
		Converter.Convert(Result.Mesh.Get(), *MeshDescription);
	});

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
