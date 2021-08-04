// Copyright Epic Games, Inc. All Rights Reserved.


#include "InteractiveToolsContext.h"
#include "ToolTargetManager.h"
#include "ContextObjectStore.h"

UInteractiveToolsContext::UInteractiveToolsContext()
{
	InputRouter = nullptr;
	ToolManager = nullptr;
	GizmoManager = nullptr;
	TargetManager = nullptr;
	ContextObjectStore = nullptr;
	ToolManagerClass = UInteractiveToolManager::StaticClass();

	SetCreateInputRouterFunc([](const FContextInitInfo& ContextInfo)
	{
		UInputRouter* NewInputRouter = NewObject<UInputRouter>(ContextInfo.ToolsContext);
		NewInputRouter->Initialize(ContextInfo.TransactionsAPI);
		return NewInputRouter;
	});
	SetShutdownInputRouterFunc([](UInputRouter* InputRouterIn)
	{
		InputRouterIn->ForceTerminateAll();
		InputRouterIn->Shutdown();
	});

	SetCreateToolManagerFunc([this](const FContextInitInfo& ContextInfo)
	{
		UInteractiveToolManager* NewToolManager = NewObject<UInteractiveToolManager>(ContextInfo.ToolsContext, this->ToolManagerClass.Get());
		NewToolManager->Initialize(ContextInfo.QueriesAPI, ContextInfo.TransactionsAPI, ContextInfo.InputRouter);
		return NewToolManager;
	});
	SetShutdownToolManagerFunc([](UInteractiveToolManager* ToolManagerIn)
	{
		ToolManagerIn->Shutdown();
	});

	SetCreateToolTargetManagerFunc([](const FContextInitInfo& ContextInfo)
	{
		UToolTargetManager* NewTargetManager = NewObject<UToolTargetManager>(ContextInfo.ToolsContext);
		NewTargetManager->Initialize();
		return NewTargetManager;
	});
	SetShutdownToolTargetManagerFunc([](UToolTargetManager* TargetManagerIn)
	{
	});

	SetCreateGizmoManagerFunc([](const FContextInitInfo& ContextInfo)
	{
		UInteractiveGizmoManager* NewGizmoManager = NewObject<UInteractiveGizmoManager>(ContextInfo.ToolsContext);
		NewGizmoManager->Initialize(ContextInfo.QueriesAPI, ContextInfo.TransactionsAPI, ContextInfo.InputRouter);
		NewGizmoManager->RegisterDefaultGizmos();
		return NewGizmoManager;
	});
	SetShutdownGizmoManagerFunc([](UInteractiveGizmoManager* GizmoManagerIn)
	{
		GizmoManagerIn->Shutdown();
	});

	SetCreateContextStoreFunc([](const FContextInitInfo& ContextInfo)
	{
		UContextObjectStore* NewContextStore = NewObject<UContextObjectStore>(ContextInfo.ToolsContext);
		return NewContextStore;
	});
	SetShutdownContextStoreFunc([](UContextObjectStore* ContextStoreIn)
	{
		ContextStoreIn->Shutdown();
	});
}


void UInteractiveToolsContext::SetCreateInputRouterFunc(TUniqueFunction<UInputRouter* (const FContextInitInfo&)> Func)
{
	CreateInputRouterFunc = MoveTemp(Func);
}
void UInteractiveToolsContext::SetCreateToolManagerFunc(TUniqueFunction<UInteractiveToolManager* (const FContextInitInfo&)> Func)
{
	CreateToolManagerFunc = MoveTemp(Func);
}
void UInteractiveToolsContext::SetCreateToolTargetManagerFunc(TUniqueFunction<UToolTargetManager* (const FContextInitInfo&)> Func)
{
	CreateToolTargetManagerFunc = MoveTemp(Func);
}
void UInteractiveToolsContext::SetCreateGizmoManagerFunc(TUniqueFunction<UInteractiveGizmoManager* (const FContextInitInfo&)> Func)
{
	CreateGizmoManagerFunc = MoveTemp(Func);
}
void UInteractiveToolsContext::SetCreateContextStoreFunc(TUniqueFunction<UContextObjectStore* (const FContextInitInfo&)> Func)
{
	CreateContextStoreFunc = MoveTemp(Func);
}


void UInteractiveToolsContext::SetShutdownInputRouterFunc(TUniqueFunction<void(UInputRouter*)> Func)
{
	ShutdownInputRouterFunc = MoveTemp(Func);
}
void UInteractiveToolsContext::SetShutdownToolManagerFunc(TUniqueFunction<void(UInteractiveToolManager*)> Func)
{
	ShutdownToolManagerFunc = MoveTemp(Func);
}
void UInteractiveToolsContext::SetShutdownToolTargetManagerFunc(TUniqueFunction<void(UToolTargetManager*)> Func)
{
	ShutdownToolTargetManagerFunc = MoveTemp(Func);
}
void UInteractiveToolsContext::SetShutdownGizmoManagerFunc(TUniqueFunction<void(UInteractiveGizmoManager*)> Func)
{
	ShutdownGizmoManagerFunc = MoveTemp(Func);
}
void UInteractiveToolsContext::SetShutdownContextStoreFunc(TUniqueFunction<void(UContextObjectStore*)> Func)
{
	ShutdownContextStoreFunc = MoveTemp(Func);
}


void UInteractiveToolsContext::Initialize(IToolsContextQueriesAPI* QueriesAPI, IToolsContextTransactionsAPI* TransactionsAPI)
{
	FContextInitInfo InitInfo;
	InitInfo.ToolsContext = this;
	InitInfo.QueriesAPI = QueriesAPI;
	InitInfo.TransactionsAPI = TransactionsAPI;

	InputRouter = CreateInputRouterFunc(InitInfo);
	InitInfo.InputRouter = InputRouter;

	// Context store needs to be built before managers in case they want to use it
	ContextObjectStore = CreateContextStoreFunc(InitInfo);
	ToolManager = CreateToolManagerFunc(InitInfo);
	TargetManager = CreateToolTargetManagerFunc(InitInfo);
	GizmoManager = CreateGizmoManagerFunc(InitInfo);
}


void UInteractiveToolsContext::Shutdown()
{
	ShutdownInputRouterFunc(InputRouter);
	InputRouter = nullptr;

	ShutdownGizmoManagerFunc(GizmoManager);
	GizmoManager = nullptr;

	ShutdownToolManagerFunc(ToolManager);
	ToolManager = nullptr;

	ShutdownToolTargetManagerFunc(TargetManager);
	TargetManager = nullptr;

	ShutdownContextStoreFunc(ContextObjectStore);
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
