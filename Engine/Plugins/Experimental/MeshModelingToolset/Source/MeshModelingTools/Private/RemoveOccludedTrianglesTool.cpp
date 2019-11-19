// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RemoveOccludedTrianglesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "AssetGenerationUtil.h"


#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif



#define LOCTEXT_NAMESPACE "URemoveOccludedTrianglesTool"


/*
 * ToolBuilder
 */


bool URemoveOccludedTrianglesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* URemoveOccludedTrianglesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	URemoveOccludedTrianglesTool* NewTool = NewObject<URemoveOccludedTrianglesTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if ( MeshComponent )
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}




/*
 * Tool
 */

URemoveOccludedTrianglesToolProperties::URemoveOccludedTrianglesToolProperties()
{

}

URemoveOccludedTrianglesAdvancedProperties::URemoveOccludedTrianglesAdvancedProperties()
{
}


URemoveOccludedTrianglesTool::URemoveOccludedTrianglesTool()
{
}

void URemoveOccludedTrianglesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void URemoveOccludedTrianglesTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	BasicProperties = NewObject<URemoveOccludedTrianglesToolProperties>(this, TEXT("Remove Occluded Triangle Settings"));
	AdvancedProperties = NewObject<URemoveOccludedTrianglesAdvancedProperties>(this, TEXT("Advanced Settings"));

	// initialize our properties
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(AdvancedProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreviews();
}


void URemoveOccludedTrianglesTool::SetupPreviews()
{	
	int32 TargetNumPreview = ComponentTargets.Num();

	CombinedMeshTrees = MakeShared<IndexMeshWithAcceleration>();

#if WITH_EDITOR
	static const FText SlowTaskText = LOCTEXT("RemoveOccludedTrianglesInit", "Building mesh occlusion data...");

	FScopedSlowTask SlowTask(TargetNumPreview, SlowTaskText);
	SlowTask.MakeDialog();

	// Declare progress shortcut lambdas
	auto EnterProgressFrame = [&SlowTask](float Progress)
	{
		SlowTask.EnterProgressFrame(Progress);
	};
#else
	auto EnterProgressFrame = [](float Progress) {};
#endif

	OriginalDynamicMeshes.SetNum(TargetNumPreview);
	for (int32 PreviewIdx = 0; PreviewIdx < TargetNumPreview; PreviewIdx++)
	{
		EnterProgressFrame(.5);

		URemoveOccludedTrianglesOperatorFactory *OpFactory = NewObject<URemoveOccludedTrianglesOperatorFactory>();
		OpFactory->Tool = this;
		OpFactory->ComponentIndex = PreviewIdx;
		OriginalDynamicMeshes[PreviewIdx] = MakeShared<FDynamicMesh3>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.bPrintDebugMessages = true;
		Converter.Convert(ComponentTargets[PreviewIdx]->GetMesh(), *OriginalDynamicMeshes[PreviewIdx]);

		EnterProgressFrame(.5);

		UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview"));
		Preview->Setup(this->TargetWorld, OpFactory);

		FComponentMaterialSet MaterialSet;
		ComponentTargets[PreviewIdx]->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		Preview->PreviewMesh->SetTransform(ComponentTargets[PreviewIdx]->GetWorldTransform());
		Preview->PreviewMesh->UpdatePreview(OriginalDynamicMeshes[PreviewIdx].Get());
		Preview->SetVisibility(true);

		CombinedMeshTrees->AddMesh(*OriginalDynamicMeshes[PreviewIdx], FTransform3d(ComponentTargets[PreviewIdx]->GetWorldTransform()));
	}

	CombinedMeshTrees->BuildAcceleration();

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}



void URemoveOccludedTrianglesTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	TArray<FDynamicMeshOpResult> Results;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Results.Add(Preview->Shutdown());
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}
}

void URemoveOccludedTrianglesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> URemoveOccludedTrianglesOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FRemoveOccludedTrianglesOp> Op = MakeUnique<FRemoveOccludedTrianglesOp>();
	Op->NormalOffset = Tool->AdvancedProperties->NormalOffset;
	switch (Tool->BasicProperties->OcclusionTestMethod)
	{
	case EOcclusionCalculationUIMode::GeneralizedWindingNumber:
		Op->InsideMode = EOcclusionCalculationMode::FastWindingNumber;
		break;
	case EOcclusionCalculationUIMode::RaycastOcclusionSamples:
		Op->InsideMode = EOcclusionCalculationMode::SimpleOcclusionTest;
		break;
	default:
		ensure(false); // all cases should be handled
	}
	switch (Tool->BasicProperties->TriangleSampling)
	{
	case EOcclusionTriangleSamplingUIMode::Vertices:
		Op->TriangleSamplingMethod = EOcclusionTriangleSampling::Vertices;
		break;
// Centroids sampling not exposed in UI for now
// 	case EOcclusionTriangleSamplingUIMode::Centroids:
// 		Op->TriangleSamplingMethod = EOcclusionTriangleSampling::Centroids;
// 		break;
	case EOcclusionTriangleSamplingUIMode::VerticesAndCentroids:
		Op->TriangleSamplingMethod = EOcclusionTriangleSampling::VerticesAndCentroids;
		break;
	default:
		ensure(false);
	}
	Op->WindingIsoValue = Tool->BasicProperties->WindingIsoValue;

	FTransform LocalToWorld = Tool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	Op->OriginalMesh = Tool->OriginalDynamicMeshes[ComponentIndex];

	Op->CombinedMeshTrees = Tool->CombinedMeshTrees;

	Op->bOnlySelfOcclude = Tool->BasicProperties->bOnlySelfOcclude;

	Op->AddRandomRays = Tool->BasicProperties->AddRandomRays;

	Op->AddTriangleSamples = Tool->BasicProperties->AddTriangleSamples;
	
	Op->SetTransform(LocalToWorld);

	return Op;
}



void URemoveOccludedTrianglesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void URemoveOccludedTrianglesTool::Tick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}



#if WITH_EDITOR
void URemoveOccludedTrianglesTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}
#endif

void URemoveOccludedTrianglesTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}



bool URemoveOccludedTrianglesTool::HasAccept() const
{
	return true;
}

bool URemoveOccludedTrianglesTool::CanAccept() const
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		if (!Preview->HaveValidResult())
		{
			return false;
		}
	}
	return true;
}


void URemoveOccludedTrianglesTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("RemoveOccludedTrianglesToolTransactionName", "Remove Occluded Triangles"));

	check(Results.Num() == ComponentTargets.Num());
	
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		check(Results[ComponentIdx].Mesh.Get() != nullptr);
		ComponentTargets[ComponentIdx]->CommitMesh([&Results, &ComponentIdx, this](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			
			Converter.Convert(Results[ComponentIdx].Mesh.Get(), *CommitParams.MeshDescription);
		});
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
