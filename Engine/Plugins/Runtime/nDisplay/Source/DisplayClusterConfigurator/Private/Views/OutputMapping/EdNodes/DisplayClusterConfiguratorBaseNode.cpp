// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingSlot.h"
#include "DisplayClusterConfiguratorToolkit.h"

void UDisplayClusterConfiguratorBaseNode::Initialize(UObject* InObject, const FString& InNodeName, const TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot> InSlot, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	SlotPtr = InSlot;

	Initialize(InObject, InNodeName, InToolkit);
}

void UDisplayClusterConfiguratorBaseNode::Initialize(UObject* InObject, const FString& InNodeName, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{

	ObjectToEdit = InObject;
	NodeName = InNodeName;
	ToolkitPtr = InToolkit;
}

void UDisplayClusterConfiguratorBaseNode::OnSelection()
{
	TArray<UObject*> SelectedObjects;
	SelectedObjects.Add(GetObject());

	ToolkitPtr.Pin()->SelectObjects(SelectedObjects);
}

bool UDisplayClusterConfiguratorBaseNode::IsSelected()
{
	const TArray<UObject*>& SelectedObjects = ToolkitPtr.Pin()->GetSelectedObjects();

	UObject* const* SelectedObject = SelectedObjects.FindByPredicate([this](const UObject* InObject)
	{
		return InObject == GetObject();
	});

	if (SelectedObject != nullptr)
	{
		UObject* Obj = *SelectedObject;

		return Obj != nullptr;
	}

	return false;
}

const FString& UDisplayClusterConfiguratorBaseNode::GetNodeName() const
{
	return NodeName;
}

TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> UDisplayClusterConfiguratorBaseNode::GetSlot() const
{
	return SlotPtr.Pin();
}

void UDisplayClusterConfiguratorBaseNode::SetSlot(TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot> InSlot)
{
	SlotPtr = InSlot;
}
