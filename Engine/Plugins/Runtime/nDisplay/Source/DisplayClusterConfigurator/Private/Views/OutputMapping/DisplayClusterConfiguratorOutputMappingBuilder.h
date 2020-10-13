// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorOutputMappingBuilder.h"

#include "UObject/WeakObjectPtr.h"

class IDisplayClusterConfiguratorOutputMappingSlot;
class FDisplayClusterConfiguratorOutputMappingCanvasSlot;
class FDisplayClusterConfiguratorToolkit;
class UDisplayClusterConfigurationCluster;
class UDisplayClusterConfiguratorGraph;
class SGraphPanel;
class SDisplayClusterConfiguratorCanvasNode;
class SDisplayClusterConfiguratorConstraintCanvas;

class FDisplayClusterConfiguratorOutputMappingBuilder
	: public IDisplayClusterConfiguratorOutputMappingBuilder
{
public:
	FDisplayClusterConfiguratorOutputMappingBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
		UDisplayClusterConfigurationCluster* InConfigurationCluster,
		const TSharedRef<SDisplayClusterConfiguratorCanvasNode>& InCanvasNode);

	//~ Begin IDisplayClusterConfiguratorOutputMappingBuilder Interface
	virtual void Build() override;
	virtual TSharedRef<SWidget> GetCanvasWidget() const override;

	virtual const TArray<TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>>& GetAllSlots() const override;
	//~ End IDisplayClusterConfiguratorOutputMappingBuilder Interface

public:
	TSharedRef<SDisplayClusterConfiguratorConstraintCanvas> GetConstraintCanvas() const;

	UDisplayClusterConfiguratorGraph* GetEdGraph();

	TSharedPtr<SGraphPanel> GetOwnerPanel() const;

	void SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel);

	void Tick(float InDeltaTime);

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakObjectPtr<UDisplayClusterConfigurationCluster> CfgClusterPtr;

	TWeakPtr<SDisplayClusterConfiguratorCanvasNode> CanvasNodePtr;

	TSharedPtr<SDisplayClusterConfiguratorConstraintCanvas> ConstraintCanvas;

	TArray<TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>> Slots;

	TWeakObjectPtr<UDisplayClusterConfiguratorGraph> EdGraph;

	TSharedPtr<FDisplayClusterConfiguratorOutputMappingCanvasSlot> CanvasSlot;
};
