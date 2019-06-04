// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


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
