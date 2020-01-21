// Copyright Epic Games, Inc. All Rights Reserved.


#include "InteractiveToolManager.h"
#include "InteractiveToolsContext.h"



#define LOCTEXT_NAMESPACE "UInteractiveToolManager"


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
		DeactivateTool(EToolSide::Left, EToolShutdownType::Accept);
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
		TransactionsAPI->DisplayMessage( LOCTEXT("ActivateToolCanBuildFailMessage", "UInteractiveToolManager::ActivateTool: CanBuildTool returned false."), EToolMessageLevel::Internal);
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

	// emit a change so that we can undo to cancel the tool
	EmitObjectChange(this, MakeUnique<FBeginToolChange>(), LOCTEXT("ActivateToolChange", "Activate Tool"));

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

UInteractiveToolBuilder* UInteractiveToolManager::GetActiveToolBuilder(EToolSide Side)
{
	return (Side == EToolSide::Left) ? ActiveLeftBuilder : ActiveRightBuilder;
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



UInteractiveGizmoManager* UInteractiveToolManager::GetPairedGizmoManager()
{
	return Cast<UInteractiveToolsContext>(GetOuter())->GizmoManager;
}

void UInteractiveToolManager::DisplayMessage(const FText& Message, EToolMessageLevel Level)
{
	TransactionsAPI->DisplayMessage(Message, Level);
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



void UInteractiveToolManager::EmitObjectChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description)
{
	// wrap change 
	check(HasActiveTool(EToolSide::Left));
	TUniquePtr<FToolChangeWrapperChange> Wrapper = MakeUnique<FToolChangeWrapperChange>();
	Wrapper->ToolManager = this;
	Wrapper->ActiveTool = GetActiveTool(EToolSide::Left);
	Wrapper->ToolChange = MoveTemp(Change);

	TransactionsAPI->AppendChange(TargetObject, MoveTemp(Wrapper), Description );
}

bool UInteractiveToolManager::RequestSelectionChange(const FSelectedOjectsChangeList& SelectionChange)
{
	return TransactionsAPI->RequestSelectionChange(SelectionChange);
}







void FBeginToolChange::Apply(UObject* Object)
{
	// do nothing on apply, we do not want to re-enter the tool
}

void FBeginToolChange::Revert(UObject* Object)
{
	// On revert, if a tool is active, we cancel it.
	// Note that this should only happen once, because any further tool activations
	// would be pushing their own FBeginToolChange
	UInteractiveToolManager* ToolManager = CastChecked<UInteractiveToolManager>(Object);
	if (ToolManager->HasAnyActiveTool())
	{
		ToolManager->DeactivateTool(EToolSide::Left, EToolShutdownType::Cancel);
	}
}

bool FBeginToolChange::HasExpired( UObject* Object ) const
{
	UInteractiveToolManager* ToolManager = CastChecked<UInteractiveToolManager>(Object);
	return (ToolManager == nullptr) || (ToolManager->HasAnyActiveTool() == false);
}

FString FBeginToolChange::ToString() const
{
	return FString(TEXT("Begin Tool"));
}





void FToolChangeWrapperChange::Apply(UObject* Object)
{
	if (ToolChange.IsValid())
	{
		ToolChange->Apply(Object);
	}
}

void FToolChangeWrapperChange::Revert(UObject* Object)
{
	if (ToolChange.IsValid())
	{
		ToolChange->Revert(Object);
	}
}

bool FToolChangeWrapperChange::HasExpired(UObject* Object) const
{
	if (ToolChange.IsValid() && ToolManager.IsValid() && ActiveTool.IsValid())
	{
		if (ToolManager->GetActiveTool(EToolSide::Left) == ActiveTool.Get())
		{
			return false;
		}
	}
	return true;
}

FString FToolChangeWrapperChange::ToString() const
{
	if (ToolChange.IsValid())
	{
		return ToolChange->ToString();
	}
	return FString();
}




#undef LOCTEXT_NAMESPACE