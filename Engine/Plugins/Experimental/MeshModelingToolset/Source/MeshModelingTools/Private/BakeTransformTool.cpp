// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeTransformTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"
#include "MeshTransforms.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "AssetGenerationUtil.h"


#define LOCTEXT_NAMESPACE "UBakeTransformTool"


/*
 * ToolBuilder
 */


bool UBakeTransformToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UBakeTransformToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UBakeTransformTool* NewTool = NewObject<UBakeTransformTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if (MeshComponent)
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

UBakeTransformToolProperties::UBakeTransformToolProperties()
{
	bRecomputeNormals = true;
}



UBakeTransformTool::UBakeTransformTool()
{
}

void UBakeTransformTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UBakeTransformTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	for (int k = 0; k < ComponentTargets.Num(); ++k)
	{
		TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[k];
		ComponentTarget->SetOwnerVisibility(false);

		FMeshDescriptionToDynamicMesh Converter;
		TSharedPtr<FDynamicMesh3> Mesh = MakeShared<FDynamicMesh3>();
		Converter.Convert(ComponentTarget->GetMesh(), *Mesh);
		OriginalDynamicMeshes.Add(Mesh);

		FTransform3d CurTransform = FTransform3d(ComponentTarget->GetWorldTransform());
		FTransform3d BakeScaleTransform;
		BakeScaleTransform.SetScale(CurTransform.GetScale());
		CurTransform.SetScale(FVector3d::One());
		FDynamicMesh3 BakeScaleMesh(*Mesh);
		MeshTransforms::ApplyTransform(BakeScaleMesh, BakeScaleTransform);

		UPreviewMesh* Preview = NewObject<UPreviewMesh>(this);
		Preview->CreateInWorld(this->TargetWorld, FTransform::Identity);
		Preview->UpdatePreview(&BakeScaleMesh);
		Preview->SetTransform((FTransform)CurTransform);
		UMaterialInterface* Material = ComponentTarget->GetMaterial(0);
		if (Material != nullptr)
		{
			Preview->SetMaterial(Material);
		}
		Previews.Add(Preview);
	}

	BasicProperties = NewObject<UBakeTransformToolProperties>(this, TEXT("Mesh Normals Settings"));
	AddToolPropertySource(BasicProperties);

	GetToolManager()->DisplayMessage(
		LOCTEXT("BakeTransformWarning", "WARNING: This Tool will Modify the selected StaticMesh Assets! If you do not wish to modify the original Assets, please make copies in the Content Browser first!"),
		EToolMessageLevel::UserWarning);
}



void UBakeTransformTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	TArray<TUniquePtr<FDynamicMesh3>> ResultMeshes;
	for (int k = 0; k < Previews.Num(); ++k)
	{
		ResultMeshes.Add(Previews[k]->ExtractPreviewMesh());
		Previews[k]->Disconnect();
	}
	Previews.SetNum(0);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		UpdateAssets(ResultMeshes);
	}
}

void UBakeTransformTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}



void UBakeTransformTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UBakeTransformTool::Tick(float DeltaTime)
{
}



bool UBakeTransformTool::HasAccept() const
{
	return true;
}

bool UBakeTransformTool::CanAccept() const
{
	return true;
}


void UBakeTransformTool::UpdateAssets(const TArray<TUniquePtr<FDynamicMesh3>>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("BakeTransformToolTransactionName", "Bake Transforms"));

	check(Results.Num() == ComponentTargets.Num());

	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		ComponentTargets[ComponentIdx]->CommitMesh([&Results, &ComponentIdx, this](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;

			Converter.Update(Results[ComponentIdx].Get(), *CommitParams.MeshDescription, true, false);

			// otherwise just copy attributes
			Converter.UpdateAttributes(Results[ComponentIdx].Get(), *CommitParams.MeshDescription, true, false);
		});

		UPrimitiveComponent* Component = ComponentTargets[ComponentIdx]->GetOwnerComponent();
		Component->Modify();
		Component->SetRelativeScale3D(FVector(1, 1, 1));
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
