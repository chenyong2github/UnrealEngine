// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransferMeshTool.h"
#include "ComponentSourceInterfaces.h"
#include "InteractiveToolManager.h"
#include "ToolTargetManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "ModelingToolTargetUtil.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UTransferMeshTool"

/*
 * ToolBuilder
 */

bool UTransferMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 2;
}

UMultiSelectionMeshEditingTool* UTransferMeshToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UTransferMeshTool>(SceneState.ToolManager);
}

/*
 * Tool
 */

void UTransferMeshTool::Setup()
{
	UInteractiveTool::Setup();

	BasicProperties = NewObject<UTransferMeshToolProperties>(this);
	BasicProperties->RestoreProperties(this);
	AddToolPropertySource(BasicProperties);

	SetToolDisplayName(LOCTEXT("ToolName", "Transfer"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Copy Mesh from Source object to Target object"),
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
