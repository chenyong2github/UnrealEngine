// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Input/DisplayClusterConfiguratorViewInputBuilder.h"

#include "DisplayClusterConfiguratorEditorData.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"

#include "Views/TreeViews/Input/TreeItems/DisplayClusterConfiguratorTreeItemInput.h"

FDisplayClusterConfiguratorViewInputBuilder::FDisplayClusterConfiguratorViewInputBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: FDisplayClusterConfiguratorTreeBuilder(InToolkit)
{}

void FDisplayClusterConfiguratorViewInputBuilder::Build(FDisplayClusterConfiguratorTreeBuilderOutput& Output)
{
	if (UDisplayClusterConfiguratorEditorData* EditorDataPtr = ConfiguratorTreePtr.Pin()->GetEditorData())
	{
		if (UDisplayClusterConfigurationData* Config = ToolkitPtr.Pin()->GetConfig())
		{
			if (Config->Input != nullptr)
			{
				AddInput(Output, Config, Config->Input);
			}
		}
	}
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInput(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, UObject* InObjectToEdit)
{
	FName ParentName = NAME_None;
	const FName NodeName = "Input";
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.Input", true);
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItem::GetTypeId());

	if (InConfig->Input->AnalogDevices.Num() > 0)
	{
		AddInputVRPNAnalogContainer(Output, InConfig, TEXT("VRPN Analogs"), NodeName.ToString(), nullptr);

		for (const auto& it : InConfig->Input->AnalogDevices)
		{
			AddInputVRPNAnalog(Output, InConfig, it.Key, TEXT("VRPN Analogs"), it.Value);
		}
	}

	if (InConfig->Input->ButtonDevices.Num() > 0)
	{
		AddInputVRPNButtonContainer(Output, InConfig, TEXT("VRPN Button"), NodeName.ToString(), nullptr);

		for (const auto& it : InConfig->Input->ButtonDevices)
		{
			AddInputVRPNButton(Output, InConfig, it.Key, TEXT("VRPN Button"), it.Value);
		}
	}

	if (InConfig->Input->KeyboardDevices.Num() > 0)
	{
		AddInputVRPNKeyboardContainer(Output, InConfig, TEXT("VRPN Keyboard"), NodeName.ToString(), nullptr);

		for (const auto& it : InConfig->Input->KeyboardDevices)
		{
			AddInputVRPNKeyboard(Output, InConfig, it.Key, TEXT("VRPN Keyboard"), it.Value);
		}
	}

	if (InConfig->Input->TrackerDevices.Num() > 0)
	{
		AddInputVRPNTrackerContainer(Output, InConfig, TEXT("VRPN Tracker"), NodeName.ToString(), nullptr);

		for (const auto& it : InConfig->Input->TrackerDevices)
		{
			AddInputVRPNTracker(Output, InConfig, it.Key, TEXT("VRPN Tracker"), it.Value);
		}
	}
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInputVRPNAnalogContainer(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.InputContainer");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemInput::GetTypeId());
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInputVRPNButtonContainer(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.InputContainer");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemInput::GetTypeId());
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInputVRPNKeyboardContainer(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.InputContainer");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemInput::GetTypeId());
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInputVRPNTrackerContainer(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.InputContainer");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemInput::GetTypeId());
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInputVRPNAnalog(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.InputDeviceAnalog");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemInput::GetTypeId());
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInputVRPNButton(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.InputDeviceButton");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemInput::GetTypeId());
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInputVRPNKeyboard(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.InputDeviceKeyboard");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemInput::GetTypeId());
}

void FDisplayClusterConfiguratorViewInputBuilder::AddInputVRPNTracker(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemInput>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.InputDeviceTracker");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemInput::GetTypeId());
}
