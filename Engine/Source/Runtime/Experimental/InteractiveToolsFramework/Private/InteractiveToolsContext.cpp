// Copyright Epic Games, Inc. All Rights Reserved.


#include "InteractiveToolsContext.h"


UInteractiveToolsContext::UInteractiveToolsContext()
{
	InputRouter = nullptr;
	ToolManager = nullptr;
}

void UInteractiveToolsContext::Initialize(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI)
{
	InputRouter = NewObject<UInputRouter>(this);
	InputRouter->Initialize(TransactionsAPI);

	ToolManager = NewObject<UInteractiveToolManager>(this);
	ToolManager->Initialize(QueriesAPI, TransactionsAPI, InputRouter);

	GizmoManager = NewObject<UInteractiveGizmoManager>(this);
	GizmoManager->Initialize(QueriesAPI, TransactionsAPI, InputRouter);

	GizmoManager->RegisterDefaultGizmos();
}


void UInteractiveToolsContext::Shutdown()
{
	// force-terminate any remaining captures/hovers/etc
	InputRouter->ForceTerminateAll();
	InputRouter->Shutdown();
	InputRouter = nullptr;

	GizmoManager->Shutdown();
	GizmoManager = nullptr;

	ToolManager->Shutdown();
	ToolManager = nullptr;
}

void UInteractiveToolsContext::DeactivateActiveTool(EToolSide WhichSide, EToolShutdownType ShutdownType)
{
	ToolManager->DeactivateTool(WhichSide, ShutdownType);
}

void UInteractiveToolsContext::DeactivateAllActiveTools()
{
	if (ToolManager->HasActiveTool(EToolSide::Left))
	{
		EToolShutdownType ShutdownType = ToolManager->CanAcceptActiveTool(EToolSide::Left) ?
			EToolShutdownType::Accept : EToolShutdownType::Cancel;
		ToolManager->DeactivateTool(EToolSide::Left, ShutdownType);
	}
	if (ToolManager->HasActiveTool(EToolSide::Right))
	{
		EToolShutdownType ShutdownType = ToolManager->CanAcceptActiveTool(EToolSide::Right) ?
			EToolShutdownType::Accept : EToolShutdownType::Cancel;
		ToolManager->DeactivateTool(EToolSide::Right, ShutdownType);
	}
}


bool UInteractiveToolsContext::CanStartTool(EToolSide WhichSide, const FString& ToolTypeIdentifier) const
{
	return (ToolManager->HasActiveTool(WhichSide) == false) &&
		(ToolManager->CanActivateTool(WhichSide, ToolTypeIdentifier) == true);
}

bool UInteractiveToolsContext::ActiveToolHasAccept(EToolSide WhichSide) const
{
	return ToolManager->HasActiveTool(WhichSide) &&
		ToolManager->GetActiveTool(WhichSide)->HasAccept();
}

bool UInteractiveToolsContext::CanAcceptActiveTool(EToolSide WhichSide) const
{
	return ToolManager->CanAcceptActiveTool(WhichSide);
}

bool UInteractiveToolsContext::CanCancelActiveTool(EToolSide WhichSide) const
{
	return ToolManager->CanCancelActiveTool(WhichSide);
}

bool UInteractiveToolsContext::CanCompleteActiveTool(EToolSide WhichSide) const
{
	return ToolManager->HasActiveTool(WhichSide) && CanCancelActiveTool(WhichSide) == false;
}

bool UInteractiveToolsContext::StartTool(EToolSide WhichSide, const FString& ToolTypeIdentifier)
{
	if (ToolManager->SelectActiveToolType(WhichSide, ToolTypeIdentifier) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("ToolManager: Unknown Tool Type %s"), *ToolTypeIdentifier);
		return false;
	}
	else
	{
		ToolManager->ActivateTool(WhichSide);
		return true;
	}
}

void UInteractiveToolsContext::EndTool(EToolSide WhichSide, EToolShutdownType ShutdownType)
{
	DeactivateActiveTool(WhichSide, ShutdownType);
}
