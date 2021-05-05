// Copyright Epic Games, Inc. All Rights Reserved.


#include "InteractiveToolsContext.h"
#include "ToolTargetManager.h"
#include "ContextObjectStore.h"

UInteractiveToolsContext::UInteractiveToolsContext()
{
	InputRouter = nullptr;
	ToolManager = nullptr;
	TargetManager = nullptr;
	ToolManagerClass = UInteractiveToolManager::StaticClass();
}

void UInteractiveToolsContext::Initialize(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI)
{
	InputRouter = NewObject<UInputRouter>(this);
	InputRouter->Initialize(TransactionsAPI);

	ToolManager = NewObject<UInteractiveToolManager>(this, ToolManagerClass.Get());
	ToolManager->Initialize(QueriesAPI, TransactionsAPI, InputRouter);

	TargetManager = NewObject<UToolTargetManager>(this);
	TargetManager->Initialize();

	GizmoManager = NewObject<UInteractiveGizmoManager>(this);
	GizmoManager->Initialize(QueriesAPI, TransactionsAPI, InputRouter);

	GizmoManager->RegisterDefaultGizmos();

	ContextObjectStore = NewObject<UContextObjectStore>(this);
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

	ContextObjectStore->Shutdown();
	ContextObjectStore = nullptr;
}

void UInteractiveToolsContext::DeactivateActiveTool(EToolSide WhichSide, EToolShutdownType ShutdownType)
{
	ToolManager->DeactivateTool(WhichSide, ShutdownType);
}

void UInteractiveToolsContext::DeactivateAllActiveTools(EToolShutdownType ShutdownType)
{
	auto DeactivateTool = [this, ShutdownType](EToolSide WhichSide) {
		if (ToolManager->HasActiveTool(WhichSide))
		{
			const EToolShutdownType AcceptOrCancel =
				ShutdownType != EToolShutdownType::Cancel && ToolManager->CanAcceptActiveTool(WhichSide)
				? EToolShutdownType::Accept : EToolShutdownType::Cancel;
			ToolManager->DeactivateTool(WhichSide, AcceptOrCancel);
		}
	};
	
	DeactivateTool(EToolSide::Left);
	DeactivateTool(EToolSide::Right);
}


bool UInteractiveToolsContext::CanStartTool(EToolSide WhichSide, const FString& ToolTypeIdentifier) const
{
	return ToolManager->CanActivateTool(WhichSide, ToolTypeIdentifier);
}

bool UInteractiveToolsContext::HasActiveTool(EToolSide WhichSide) const
{
	return ToolManager->HasActiveTool(WhichSide);
}

FString UInteractiveToolsContext::GetActiveToolName(EToolSide WhichSide) const
{
	return ToolManager->GetActiveToolName(WhichSide);
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

// Note: this takes FString by value so that it can be bound to delegates using CreateUObject. If you change it
// to a reference, you'll need to use CreateWeakLambda instead and capture ToolIdentifier by value there.
bool UInteractiveToolsContext::IsToolActive(EToolSide WhichSide, const FString ToolIdentifier) const
{
	return GetActiveToolName(WhichSide) == ToolIdentifier;
}

void UInteractiveToolsContext::PostToolNotificationMessage(const FText& Message)
{
	OnToolNotificationMessage.Broadcast(Message);
}

void UInteractiveToolsContext::PostToolWarningMessage(const FText& Message)
{
	OnToolWarningMessage.Broadcast(Message);
}
