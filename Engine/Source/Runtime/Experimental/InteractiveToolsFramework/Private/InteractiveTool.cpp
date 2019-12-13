// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "InteractiveTool.h"
#include "InteractiveToolManager.h"


UInteractiveTool::UInteractiveTool()
{
	// tools need to be transactional or undo/redo won't work on their uproperties
	SetFlags(RF_Transactional);

	// tools don't get saved but this isn't necessary because they are created in the transient package...
	//SetFlags(RF_Transient);

	InputBehaviors = NewObject<UInputBehaviorSet>(this, TEXT("InputBehaviors"));
}

void UInteractiveTool::Setup()
{
}

void UInteractiveTool::Shutdown(EToolShutdownType ShutdownType)
{
	InputBehaviors->RemoveAll();
	ToolPropertyObjects.Reset();
}

void UInteractiveTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}


void UInteractiveTool::AddInputBehavior(UInputBehavior* Behavior)
{
	InputBehaviors->Add(Behavior);
}

const UInputBehaviorSet* UInteractiveTool::GetInputBehaviors() const
{
	return InputBehaviors;
}


void UInteractiveTool::AddToolPropertySource(UObject* PropertyObject)
{
	check(ToolPropertyObjects.Contains(PropertyObject) == false);
	ToolPropertyObjects.Add(PropertyObject);
}

void UInteractiveTool::AddToolPropertySource(UInteractiveToolPropertySet* PropertySet)
{
	check(ToolPropertyObjects.Contains(PropertySet) == false);
	ToolPropertyObjects.Add(PropertySet);
	// @todo do we need to create a lambda every time for this?
	PropertySet->GetOnModified().AddLambda([this](UObject* PropertySetArg, FProperty* PropertyArg)
	{
		OnPropertyModified(PropertySetArg, PropertyArg);
	});
}


const TArray<UObject*>& UInteractiveTool::GetToolProperties() const
{
	return ToolPropertyObjects;
}


void UInteractiveTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
}

FInteractiveToolActionSet* UInteractiveTool::GetActionSet()
{
	if (ToolActionSet == nullptr)
	{
		ToolActionSet = new FInteractiveToolActionSet();
		RegisterActions(*ToolActionSet);
	}
	return ToolActionSet;
}

void UInteractiveTool::ExecuteAction(int32 ActionID)
{
	GetActionSet()->ExecuteAction(ActionID);
}



bool UInteractiveTool::HasCancel() const
{
	return false;
}

bool UInteractiveTool::HasAccept() const
{
	return false;
}

bool UInteractiveTool::CanAccept() const
{
	return false;
}


void UInteractiveTool::Tick(float DeltaTime)
{
}

UInteractiveToolManager* UInteractiveTool::GetToolManager() const
{
	UInteractiveToolManager* ToolManager = Cast<UInteractiveToolManager>(GetOuter());
	check(ToolManager != nullptr);
	return ToolManager;
}