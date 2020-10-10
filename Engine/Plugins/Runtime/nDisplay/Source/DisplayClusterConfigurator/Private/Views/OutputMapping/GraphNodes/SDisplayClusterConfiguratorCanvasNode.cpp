// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorCanvasNode.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorCanvasNode.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorOutputMappingBuilder.h"
#include "SGraphPanel.h"

void SDisplayClusterConfiguratorCanvasNode::Construct(const FArguments& InArgs, UDisplayClusterConfiguratorCanvasNode* InNode, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{	
	SDisplayClusterConfiguratorBaseNode::Construct(SDisplayClusterConfiguratorBaseNode::FArguments(), InNode, InToolkit);

	CanvasNodePtr = InNode;
	CfgClusterPtr = CanvasNodePtr.Get()->GetCfgCluster();
	OutputMappingBuilder = MakeShared<FDisplayClusterConfiguratorOutputMappingBuilder>(InToolkit, CfgClusterPtr.Get(), SharedThis(this));
	OutputMappingBuilder->Build();

	UpdateGraphNode();
}

void SDisplayClusterConfiguratorCanvasNode::UpdateGraphNode()
{
	SDisplayClusterConfiguratorBaseNode::UpdateGraphNode();
	
	GetOrAddSlot( ENodeZone::Center )
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	[
		OutputMappingBuilder->GetCanvasWidget()
	];
}

void SDisplayClusterConfiguratorCanvasNode::SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel)
{
	SGraphNode::SetOwner(OwnerPanel);
	OwnerPanel->AttachGraphEvents(SharedThis(this));

	OutputMappingBuilder->SetOwner(OwnerPanel);
}

void SDisplayClusterConfiguratorCanvasNode::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	OutputMappingBuilder->Tick(InDeltaTime);
}

UObject* SDisplayClusterConfiguratorCanvasNode::GetEditingObject() const
{
	return CanvasNodePtr->GetObject();
}

void SDisplayClusterConfiguratorCanvasNode::OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem)
{
	UObject* SelectedObject = InTreeItem->GetObject();

	// Select this node
	if (UObject* NodeObject = GetEditingObject())
	{
		if (NodeObject == SelectedObject)
		{
			InNodeVisibile = true;
			return;
		}
	}

	InNodeVisibile = false;
}

const TArray<TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>>& SDisplayClusterConfiguratorCanvasNode::GetAllSlots() const
{
	return OutputMappingBuilder->GetAllSlots();
}
