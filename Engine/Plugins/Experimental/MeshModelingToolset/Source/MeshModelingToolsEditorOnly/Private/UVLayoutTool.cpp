// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVLayoutTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "ParameterizationOps/UVLayoutOp.h"

#include "AssetGenerationUtil.h"

#define LOCTEXT_NAMESPACE "UUVLayoutTool"


/*
 * ToolBuilder
 */


bool UUVLayoutToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) >= 1;
}

UInteractiveTool* UUVLayoutToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVLayoutTool* NewTool = NewObject<UUVLayoutTool>(SceneState.ToolManager);

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
	NewTool->SetWorld(SceneState.World, SceneState.GizmoManager);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}



/*
 * Tool
 */

UUVLayoutToolProperties::UUVLayoutToolProperties()
{

}

UUVLayoutAdvancedProperties::UUVLayoutAdvancedProperties()
{
}


UUVLayoutTool::UUVLayoutTool()
{
}

void UUVLayoutTool::SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManagerIn)
{
	this->TargetWorld = World;
}

void UUVLayoutTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	BasicProperties = NewObject<UUVLayoutToolProperties>(this, TEXT("UV Projection Settings"));
	AdvancedProperties = NewObject<UUVLayoutAdvancedProperties>(this, TEXT("Advanced Settings"));

	// initialize our properties
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(AdvancedProperties);

	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->Setup();
	AddToolPropertySource(MaterialSettings);

	// initialize the PreviewMesh+BackgroundCompute object
	UpdateNumPreviews();
	
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UUVLayoutTool::UpdateNumPreviews()
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
			UUVLayoutOperatorFactory *OpFactory = NewObject<UUVLayoutOperatorFactory>();
			OpFactory->Tool = this;
			OpFactory->ComponentIndex = PreviewIdx;
			OriginalDynamicMeshes[PreviewIdx] = MakeShared<FDynamicMesh3>();
			FMeshDescriptionToDynamicMesh Converter;
			Converter.bPrintDebugMessages = true;
			Converter.Convert(ComponentTargets[PreviewIdx]->GetMesh(), *OriginalDynamicMeshes[PreviewIdx]);

			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview"));
			Preview->Setup(this->TargetWorld, OpFactory);

			FComponentMaterialSet MaterialSet;
			ComponentTargets[PreviewIdx]->GetMaterialSet(MaterialSet);
			Preview->ConfigureMaterials(MaterialSet.Materials,
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);

			Preview->SetVisibility(true);
		}
	}
}


void UUVLayoutTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	TArray<FDynamicMeshOpResult> Results;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Results.Emplace(Preview->Shutdown());
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}
}

void UUVLayoutTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UUVLayoutOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FUVLayoutOp> Op = MakeUnique<FUVLayoutOp>();

	FTransform LocalToWorld = Tool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	Op->OriginalMesh = Tool->OriginalDynamicMeshes[ComponentIndex];
	
	Op->SetTransform(LocalToWorld);

	return Op;
}



void UUVLayoutTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UUVLayoutTool::Tick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}



#if WITH_EDITOR
void UUVLayoutTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}
#endif

void UUVLayoutTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	// if we don't know what changed, or we know checker density changed, update checker material
	MaterialSettings->UpdateMaterials();
	for (int PreviewIdx = 0; PreviewIdx < Previews.Num(); PreviewIdx++)
	{
		UMeshOpPreviewWithBackgroundCompute* Preview = Previews[PreviewIdx];
		Preview->OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();
	}
	
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}

bool UUVLayoutTool::HasAccept() const
{
	return true;
}

bool UUVLayoutTool::CanAccept() const
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


void UUVLayoutTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("UVLayoutToolTransactionName", "UV Layout Tool"));

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
