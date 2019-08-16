// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "InteractiveToolManager.h"


UInteractiveToolManager::UInteractiveToolManager()
{
	QueriesAPI = nullptr;
	TransactionsAPI = nullptr;
	InputRouter = nullptr;

	ActiveLeftBuilder = nullptr;
	ActiveLeftTool = nullptr;

	ActiveRightBuilder = nullptr;
	ActiveRightTool = nullptr;
}


void UInteractiveToolManager::Initialize(IToolsContextQueriesAPI* queriesAPI, IToolsContextTransactionsAPI* transactionsAPI, UInputRouter* InputRouterIn)
{
	this->QueriesAPI = queriesAPI;
	this->TransactionsAPI = transactionsAPI;
	this->InputRouter = InputRouterIn;
}


void UInteractiveToolManager::Shutdown()
{
	this->QueriesAPI = nullptr;

	if (ActiveLeftTool != nullptr)
	{
		DeactivateTool(EToolSide::Left, EToolShutdownType::Cancel);
	}
	if (ActiveRightTool != nullptr)
	{
		DeactivateTool(EToolSide::Right, EToolShutdownType::Cancel);
	}

	this->TransactionsAPI = nullptr;
}



void UInteractiveToolManager::RegisterToolType(const FString& Identifier, UInteractiveToolBuilder* Builder)
{
	check(ToolBuilders.Contains(Identifier) == false);
	ToolBuilders.Add(Identifier, Builder );
}


bool UInteractiveToolManager::SelectActiveToolType(EToolSide Side, const FString& Identifier)
{
	if (ToolBuilders.Contains(Identifier))
	{
		UInteractiveToolBuilder* Builder = ToolBuilders[Identifier];
		if (Side == EToolSide::Right)
		{
			ActiveRightBuilder = Builder;
		}
		else
		{
			ActiveLeftBuilder = Builder;
		}
		return true;
	}
	return false;
}



bool UInteractiveToolManager::CanActivateTool(EToolSide Side, const FString& Identifier)
{
	check(Side == EToolSide::Left);   // TODO: support right-side tool

	if (ActiveLeftTool != nullptr)
	{
		return false;
	}

	if (ToolBuilders.Contains(Identifier))
	{
		FToolBuilderState InputState;
		QueriesAPI->GetCurrentSelectionState(InputState);

		UInteractiveToolBuilder* Builder = ToolBuilders[Identifier];
		return Builder->CanBuildTool(InputState);
	}

	return false;
}


bool UInteractiveToolManager::ActivateTool(EToolSide Side)
{
	check(Side == EToolSide::Left);   // TODO: support right-side tool

	if (ActiveLeftTool != nullptr) 
	{
		DeactivateTool(EToolSide::Left, EToolShutdownType::Cancel);
	}

	if (ActiveLeftBuilder == nullptr) 
	{
		return false;
	}

	// construct input state we will pass to tools
	FToolBuilderState InputState;
	QueriesAPI->GetCurrentSelectionState(InputState);

	if (ActiveLeftBuilder->CanBuildTool(InputState) == false)
	{
		TransactionsAPI->PostMessage( TEXT("UInteractiveToolManager::ActivateTool: CanBuildTool returned false."), EToolMessageLevel::Internal);
		return false;
	}

	ActiveLeftTool = ActiveLeftBuilder->BuildTool(InputState);
	if (ActiveLeftTool == nullptr)
	{
		return false;
	}

	ActiveLeftTool->Setup();

	// register new active input behaviors
	InputRouter->RegisterSource(ActiveLeftTool);

	PostInvalidation();

	OnToolStarted.Broadcast(this, ActiveLeftTool);

	return true;
}


void UInteractiveToolManager::DeactivateTool(EToolSide Side, EToolShutdownType ShutdownType)
{
	check(Side == EToolSide::Left);   // TODO: support right-side tool

	if (ActiveLeftTool != nullptr)
	{
		InputRouter->ForceTerminateSource(ActiveLeftTool);

		ActiveLeftTool->Shutdown(ShutdownType);

		InputRouter->DeregisterSource(ActiveLeftTool);

		UInteractiveTool* DoneTool = ActiveLeftTool;
		ActiveLeftTool = nullptr;

		PostInvalidation();

		OnToolEnded.Broadcast(this, DoneTool);
	}
}


bool UInteractiveToolManager::HasActiveTool(EToolSide Side) const
{
	return (Side == EToolSide::Left) ? (ActiveLeftTool != nullptr) : (ActiveRightTool != nullptr);
}

bool UInteractiveToolManager::HasAnyActiveTool() const
{
	return ActiveLeftTool != nullptr || ActiveRightTool != nullptr;
}


UInteractiveTool* UInteractiveToolManager::GetActiveTool(EToolSide Side)
{
	return (Side == EToolSide::Left) ? ActiveLeftTool : ActiveRightTool;
}


bool UInteractiveToolManager::CanAcceptActiveTool(EToolSide Side)
{
	if (ActiveLeftTool != nullptr)
	{
		return ActiveLeftTool->HasAccept() && ActiveLeftTool->CanAccept();
	}
	return false;
}

bool UInteractiveToolManager::CanCancelActiveTool(EToolSide Side)
{
	if (ActiveLeftTool != nullptr)
	{
		return ActiveLeftTool->HasCancel();
	}
	return false;
}






void UInteractiveToolManager::Tick(float DeltaTime)
{
	if (ActiveLeftTool != nullptr)
	{
		ActiveLeftTool->Tick(DeltaTime);
	}

	if (ActiveRightTool!= nullptr)
	{
		ActiveRightTool->Tick(DeltaTime);
	}
}


void UInteractiveToolManager::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (ActiveLeftTool != nullptr)
	{
		ActiveLeftTool->Render(RenderAPI);
	}

	if (ActiveRightTool != nullptr)
	{
		ActiveRightTool->Render(RenderAPI);
	}
}


void UInteractiveToolManager::PostMessage(const TCHAR* Message, EToolMessageLevel Level)
{
	TransactionsAPI->PostMessage(Message, Level);
}

void UInteractiveToolManager::PostMessage(const FString& Message, EToolMessageLevel Level)
{
	TransactionsAPI->PostMessage(*Message, Level);
}

void UInteractiveToolManager::PostInvalidation()
{
	TransactionsAPI->PostInvalidation();
}


void UInteractiveToolManager::BeginUndoTransaction(const FText& Description)
{
	TransactionsAPI->BeginUndoTransaction(Description);
}

void UInteractiveToolManager::EndUndoTransaction()
{
	TransactionsAPI->EndUndoTransaction();
}



void UInteractiveToolManager::EmitObjectChange(UObject* TargetObject, TUniquePtr<FChange> Change, const FText& Description)
{
	TransactionsAPI->AppendChange(TargetObject, MoveTemp(Change), Description );
}

bool UInteractiveToolManager::RequestSelectionChange(const FSelectedOjectsChangeList& SelectionChange)
{
	return TransactionsAPI->RequestSelectionChange(SelectionChange);
}
