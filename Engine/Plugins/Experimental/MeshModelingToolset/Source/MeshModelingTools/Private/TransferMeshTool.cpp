// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransferMeshTool.h"
#include "ComponentSourceInterfaces.h"
#include "InteractiveToolManager.h"
#include "ToolTargetManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "ModelingToolTargetUtil.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UTransferMeshTool"

/*
 * ToolBuilder
 */
const FToolTargetTypeRequirements& UTransferMeshToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMaterialProvider::StaticClass(),
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UTransferMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 2;
}

UInteractiveTool* UTransferMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UTransferMeshTool* NewTool = NewObject<UTransferMeshTool>(SceneState.ToolManager);
	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
	NewTool->SetWorld(SceneState.World);
	return NewTool;
}

/*
 * Tool
 */

UTransferMeshTool::UTransferMeshTool(const FObjectInitializer&)
{
}

void UTransferMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UTransferMeshTool::Setup()
{
	UInteractiveTool::Setup();

	BasicProperties = NewObject<UTransferMeshToolProperties>(this);
	BasicProperties->RestoreProperties(this);
	AddToolPropertySource(BasicProperties);

	SetToolDisplayName(LOCTEXT("ToolName", "Transfer"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Copy mesh from Source object to Target object"),
		EToolMessageLevel::UserNotification);
}

bool UTransferMeshTool::CanAccept() const
{
	return Super::CanAccept();
}

void UTransferMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	BasicProperties->SaveProperties(this);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("TransferMeshToolTransactionName", "Transfer Mesh"));

		const FMeshDescription* SourceMesh = UE::ToolTarget::GetMeshDescription(Targets[0]);

		FComponentMaterialSet Materials = UE::ToolTarget::GetMaterialSet(Targets[0]);
		const FComponentMaterialSet* TransferMaterials = (BasicProperties->bTransferMaterials) ? &Materials : nullptr;

		if (ensure(SourceMesh))
		{
			UE::ToolTarget::CommitMeshDescriptionUpdate(Targets[1], SourceMesh, TransferMaterials);
		}

		GetToolManager()->EndUndoTransaction();
	}
}





#undef LOCTEXT_NAMESPACE
