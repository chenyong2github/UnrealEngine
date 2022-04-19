// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfaceGraph_EdGraph.h"
#include "DataInterfaceGraph_EditorData.h"
#include "DataInterfaceUncookedOnlyUtils.h"

void UDataInterfaceGraph_EdGraph::Initialize(UDataInterfaceGraph_EditorData* InEditorData)
{
	InEditorData->ModifiedEvent.RemoveAll(this);
	InEditorData->ModifiedEvent.AddUObject(this, &UDataInterfaceGraph_EdGraph::HandleModifiedEvent);
	InEditorData->VMCompiledEvent.RemoveAll(this);
	InEditorData->VMCompiledEvent.AddUObject(this, &UDataInterfaceGraph_EdGraph::HandleVMCompiledEvent);
}

URigVMGraph* UDataInterfaceGraph_EdGraph::GetModel() const
{
	if (const UDataInterfaceGraph_EditorData* EditorData = GetTypedOuter<UDataInterfaceGraph_EditorData>())
	{
		return EditorData->GetRigVMGraph(this);
	}
	return nullptr;
}

URigVMController* UDataInterfaceGraph_EdGraph::GetController() const
{
	if (const UDataInterfaceGraph_EditorData* EditorData = GetTypedOuter<UDataInterfaceGraph_EditorData>())
	{
		return EditorData->GetRigVMController(this);
	}
	return nullptr;
}