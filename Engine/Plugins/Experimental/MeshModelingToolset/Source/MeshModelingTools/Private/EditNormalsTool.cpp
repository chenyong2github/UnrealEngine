// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditNormalsTool.h"
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


#define LOCTEXT_NAMESPACE "UEditNormalsTool"


/*
 * ToolBuilder
 */


bool UEditNormalsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UEditNormalsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UEditNormalsTool* NewTool = NewObject<UEditNormalsTool>(SceneState.ToolManager);

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

UEditNormalsToolProperties::UEditNormalsToolProperties()
{
	bFixInconsistentNormals = false;
	bInvertNormals = false;
	bRecomputeNormals = true;
	NormalCalculationMethod = ENormalCalculationMethod::AreaAngleWeighting;
	bRecomputeNormalTopologyAndEdgeSharpness = false;
	SharpEdgeAngleThreshold = 60;
	bAllowSharpVertices = false;
}

UEditNormalsAdvancedProperties::UEditNormalsAdvancedProperties()
{
}


UEditNormalsTool::UEditNormalsTool()
{
}

void UEditNormalsTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UEditNormalsTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	BasicProperties = NewObject<UEditNormalsToolProperties>(this, TEXT("Mesh Normals Settings"));
	AdvancedProperties = NewObject<UEditNormalsAdvancedProperties>(this, TEXT("Advanced Settings"));

	// initialize our properties
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(AdvancedProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	UpdateNumPreviews();

	
	
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UEditNormalsTool::UpdateNumPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 TargetNumPreview = ComponentTargets.Num();
	if (TargetNumPreview < CurrentNumPreview)
	{
		for (int32 PreviewIdx = CurrentNumPreview - 1; PreviewIdx >= TargetNumPreview; PreviewIdx--)
		{
			Previews[PreviewIdx]->Cancel();
		}
		Previews.SetNum(TargetNumPreview);
		OriginalDynamicMeshes.SetNum(TargetNumPreview);
	}
	else
	{
		OriginalDynamicMeshes.SetNum(TargetNumPreview);
		for (int32 PreviewIdx = CurrentNumPreview; PreviewIdx < TargetNumPreview; PreviewIdx++)
		{
			UEditNormalsOperatorFactory *OpFactory = NewObject<UEditNormalsOperatorFactory>();
			OpFactory->Tool = this;
			OpFactory->ComponentIndex = PreviewIdx;
			OriginalDynamicMeshes[PreviewIdx] = MakeShared<FDynamicMesh3>();
			FMeshDescriptionToDynamicMesh Converter;
			Converter.bPrintDebugMessages = true;
			Converter.Convert(ComponentTargets[PreviewIdx]->GetMesh(), *OriginalDynamicMeshes[PreviewIdx]);

			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview"));
			Preview->Setup(this->TargetWorld, OpFactory);
			Preview->ConfigureMaterials(
				ToolSetupUtil::GetDefaultMaterial(GetToolManager(), ComponentTargets[PreviewIdx]->GetMaterial(0)),
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);
			Preview->SetVisibility(true);
		}
	}
}


void UEditNormalsTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	TArray<TUniquePtr<FDynamicMeshOpResult>> Results;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Results.Emplace(Preview->Shutdown());
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}
}

void UEditNormalsTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TSharedPtr<FDynamicMeshOperator> UEditNormalsOperatorFactory::MakeNewOperator()
{
	TSharedPtr<FEditNormalsOp> NormalsOp = MakeShared<FEditNormalsOp>();
	NormalsOp->bFixInconsistentNormals = Tool->BasicProperties->bFixInconsistentNormals;
	NormalsOp->bInvertNormals = Tool->BasicProperties->bInvertNormals;
	NormalsOp->bRecomputeNormals = Tool->BasicProperties->bRecomputeNormals;
	NormalsOp->bSplitNormals = Tool->BasicProperties->bRecomputeNormalTopologyAndEdgeSharpness;
	NormalsOp->bAllowSharpVertices = Tool->BasicProperties->bAllowSharpVertices;
	NormalsOp->NormalCalculationMethod = Tool->BasicProperties->NormalCalculationMethod;
	NormalsOp->NormalSplitThreshold = Tool->BasicProperties->SharpEdgeAngleThreshold;

	FTransform LocalToWorld = Tool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	NormalsOp->OriginalMesh = Tool->OriginalDynamicMeshes[ComponentIndex];
	
	NormalsOp->SetTransform(LocalToWorld);

	return NormalsOp;
}



void UEditNormalsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UEditNormalsTool::Tick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}



#if WITH_EDITOR
void UEditNormalsTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}
#endif

void UEditNormalsTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}



bool UEditNormalsTool::HasAccept() const
{
	return true;
}

bool UEditNormalsTool::CanAccept() const
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


void UEditNormalsTool::GenerateAsset(const TArray<TUniquePtr<FDynamicMeshOpResult>>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("EditNormalsToolTransactionName", "Edit Normals Tool"));

	check(Results.Num() == ComponentTargets.Num());
	
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		check(Results[ComponentIdx]->Mesh.Get() != nullptr);
		ComponentTargets[ComponentIdx]->CommitMesh([&Results, &ComponentIdx, this](FMeshDescription* MeshDescription)
		{
			FDynamicMeshToMeshDescription Converter;
			
			if (BasicProperties->WillTopologyChange())
			{
				// full conversion if normal topology changed or faces were inverted
				Converter.Convert(Results[ComponentIdx]->Mesh.Get(), *MeshDescription);
			}
			else
			{
				// otherwise just copy attributes
				Converter.UpdateAttributes(Results[ComponentIdx]->Mesh.Get(), *MeshDescription, true, false);
			}
		});
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
