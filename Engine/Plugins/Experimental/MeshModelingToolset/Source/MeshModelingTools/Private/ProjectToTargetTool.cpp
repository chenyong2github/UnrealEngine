// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectToTargetTool.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "InteractiveToolManager.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UProjectToTargetTool"

const FToolTargetTypeRequirements& UProjectToTargetToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UMaterialProvider::StaticClass()
		});
	return TypeRequirements;
}

bool UProjectToTargetToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 2;
}

UInteractiveTool* UProjectToTargetToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UProjectToTargetTool* NewTool = NewObject<UProjectToTargetTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}

void UProjectToTargetTool::Setup()
{
	// ProjectionTarget and ProjectionTargetSpatial are setup before calling the parent class's Setup
	FMeshDescriptionToDynamicMesh ProjectionConverter;
	check(Targets.Num() == 2);
	ProjectionTarget = MakeUnique<FDynamicMesh3>();
	ProjectionConverter.Convert(TargetMeshProviderInterface(1)->GetMeshDescription(), *ProjectionTarget);
	ProjectionTargetSpatial = MakeUnique<FDynamicMeshAABBTree3>(ProjectionTarget.Get(), true);

	// Now setup parent RemeshMeshTool class
	URemeshMeshTool::Setup();

	SetToolDisplayName(LOCTEXT("ProjectToTargetToolName", "Project To Target"));
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
