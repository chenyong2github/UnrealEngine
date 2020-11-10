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
	BasicProperties->RestoreProperties(this);

	AddToolPropertySource(BasicProperties);

	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->RestoreProperties(this);
	AddToolPropertySource(MaterialSettings);

	// if we only have one object, add optional UV layout view
	if (ComponentTargets.Num() == 1)
	{
		UVLayoutView = NewObject<UUVLayoutPreview>(this);
		UVLayoutView->CreateInWorld(TargetWorld);

		FComponentMaterialSet MaterialSet;
		ComponentTargets[0]->GetMaterialSet(MaterialSet);
		UVLayoutView->SetSourceMaterials(MaterialSet);

		UVLayoutView->SetSourceWorldPosition(
			ComponentTargets[0]->GetOwnerActor()->GetTransform(),
			ComponentTargets[0]->GetOwnerActor()->GetComponentsBoundingBox());

		UVLayoutView->Settings->RestoreProperties(this);
		AddToolPropertySource(UVLayoutView->Settings);
	}

	UpdateVisualization();

	GetToolManager()->DisplayMessage(LOCTEXT("OnStartUVLayoutTool", "Transform/Rotate/Scale existing UV Islands/Shells/Charts using various strategies"),
		EToolMessageLevel::UserNotification);
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
			Converter.Convert(ComponentTargets[PreviewIdx]->GetMesh(), *OriginalDynamicMeshes[PreviewIdx]);

			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview"));
			Preview->Setup(this->TargetWorld, OpFactory);

			FComponentMaterialSet MaterialSet;
			ComponentTargets[PreviewIdx]->GetMaterialSet(MaterialSet);
			Preview->ConfigureMaterials(MaterialSet.Materials,
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);
			Preview->PreviewMesh->UpdatePreview(OriginalDynamicMeshes[PreviewIdx].Get());
			Preview->PreviewMesh->SetTransform(ComponentTargets[PreviewIdx]->GetWorldTransform());

			Preview->OnMeshUpdated.AddLambda([this](UMeshOpPreviewWithBackgroundCompute* Compute)
			{
				OnPreviewMeshUpdated(Compute);
			});

			Preview->SetVisibility(true);
		}
	}
}


void UUVLayoutTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (UVLayoutView)
	{
		UVLayoutView->Settings->SaveProperties(this);
		UVLayoutView->Disconnect();
	}

	BasicProperties->SaveProperties(this);
	MaterialSettings->SaveProperties(this);

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

	switch (Tool->BasicProperties->LayoutType)
	{
	case EUVLayoutType::Transform:
		Op->UVLayoutMode = EUVLayoutOpLayoutModes::TransformOnly;
		break;
	case EUVLayoutType::Stack:
		Op->UVLayoutMode = EUVLayoutOpLayoutModes::StackInUnitRect;
		break;
	case EUVLayoutType::Repack:
		Op->UVLayoutMode = EUVLayoutOpLayoutModes::RepackToUnitRect;
		break;
	}

	//Op->bSeparateUVIslands = Tool->BasicProperties->bSeparateUVIslands;
	Op->TextureResolution = Tool->BasicProperties->TextureResolution;
	Op->bAllowFlips = Tool->BasicProperties->bAllowFlips;
	Op->UVScaleFactor = Tool->BasicProperties->UVScaleFactor;
	Op->UVTranslation = FVector2f(Tool->BasicProperties->UVTranslate);
	Op->SetTransform(LocalToWorld);

	return Op;
}



void UUVLayoutTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	if (UVLayoutView)
	{
		UVLayoutView->Render(RenderAPI);
	}

}

void UUVLayoutTool::OnTick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}

	if (UVLayoutView)
	{
		UVLayoutView->OnTick(DeltaTime);
	}


}



void UUVLayoutTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (PropertySet == BasicProperties)
	{
		UpdateNumPreviews();
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->InvalidateResult();
		}
	}
	else if (PropertySet == MaterialSettings)
	{
		// if we don't know what changed, or we know checker density changed, update checker material
		UpdateVisualization();
	}
}


void UUVLayoutTool::OnPreviewMeshUpdated(UMeshOpPreviewWithBackgroundCompute* Compute)
{
	if (UVLayoutView)
	{
		FDynamicMesh3 ResultMesh;
		if (Compute->GetCurrentResultCopy(ResultMesh, false) == false)
		{
			return;
		}
		UVLayoutView->UpdateUVMesh(&ResultMesh);
	}

}


void UUVLayoutTool::UpdateVisualization()
{
	MaterialSettings->UpdateMaterials();
	UpdateNumPreviews();
	for (int PreviewIdx = 0; PreviewIdx < Previews.Num(); PreviewIdx++)
	{
		UMeshOpPreviewWithBackgroundCompute* Preview = Previews[PreviewIdx];
		Preview->OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();
	}

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
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
	return Super::CanAccept();
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
