// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectToTargetTool.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "InteractiveToolManager.h"

#define LOCTEXT_NAMESPACE "UProjectToTargetTool"

bool UProjectToTargetToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 2;
}

UInteractiveTool* UProjectToTargetToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UProjectToTargetTool* NewTool = NewObject<UProjectToTargetTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() == 2);

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

void UProjectToTargetTool::Setup()
{
	// ProjectionTarget and ProjectionTargetSpatial are setup before calling the parent class's Setup
	FMeshDescriptionToDynamicMesh ProjectionConverter;
	check(ComponentTargets.Num() == 2);
	FPrimitiveComponentTarget* TargetComponentTarget = ComponentTargets[1].Get();
	ProjectionTarget = MakeUnique<FDynamicMesh3>();
	ProjectionConverter.Convert(TargetComponentTarget->GetMesh(), *ProjectionTarget);
	ProjectionTargetSpatial = MakeUnique<FDynamicMeshAABBTree3>(ProjectionTarget.Get(), true);

	// Now setup parent RemeshMeshTool class
	URemeshMeshTool::Setup();

	SetToolDisplayName(LOCTEXT("ProjectToTargetToolName", "Remesh To Target Tool"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("ProjectToTargetToolDescription", "Incrementally deform the first selected mesh towards the second, while applying Remeshing. This can be used to improve the accuracy of shrink-wrapping strategies."),
		EToolMessageLevel::UserNotification);
}


TUniquePtr<FDynamicMeshOperator> UProjectToTargetTool::MakeNewOperator()
{
	TUniquePtr<FDynamicMeshOperator> Op = URemeshMeshTool::MakeNewOperator();

	FDynamicMeshOperator* RawOp = Op.Get();
	FRemeshMeshOp* RemeshOp = static_cast<FRemeshMeshOp*>(RawOp);
	check(RemeshOp);

	RemeshOp->ProjectionTarget = ProjectionTarget.Get();
	RemeshOp->ProjectionTargetSpatial = ProjectionTargetSpatial.Get();

	return Op;
}


#undef LOCTEXT_NAMESPACE
