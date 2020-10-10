// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/TreeViews/DisplayClusterConfiguratorTreeBuilder.h"

class FDisplayClusterConfiguratorToolkit;
class UDisplayClusterConfigurationData;

class FDisplayClusterConfiguratorViewInputBuilder
	: public FDisplayClusterConfiguratorTreeBuilder
{
public:
	FDisplayClusterConfiguratorViewInputBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin IDisplayClusterConfiguratorTreeBuilder Interface
	virtual void Build(FDisplayClusterConfiguratorTreeBuilderOutput& Output) override;
	//~ End IDisplayClusterConfiguratorTreeBuilder Interface

private:
	void AddInput(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, UObject* InObjectToEdit);

	void AddInputVRPNAnalogContainer(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit);
	void AddInputVRPNButtonContainer(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit);
	void AddInputVRPNKeyboardContainer(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit);
	void AddInputVRPNTrackerContainer(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit);

	void AddInputVRPNAnalog(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit);
	void AddInputVRPNButton(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit);
	void AddInputVRPNKeyboard(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit);
	void AddInputVRPNTracker(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit);
};
