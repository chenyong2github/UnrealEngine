// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/DisplayClusterConfiguratorOutputMappingBuilder.h"

#include "DisplayClusterConfiguratorOutputMappingBuilder.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorGraph.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorCanvasNode.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorConstraintCanvas.h"
#include "Views/OutputMapping/Slots/DisplayClusterConfiguratorOutputMappingCanvasSlot.h"
#include "Views/OutputMapping/Slots/DisplayClusterConfiguratorOutputMappingWindowSlot.h"
#include "Views/OutputMapping/Slots/DisplayClusterConfiguratorOutputMappingViewportSlot.h"


const FName FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Canvas		= TEXT("Canvas");
const FName FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Window		= TEXT("Window");
const FName FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Viewport	= TEXT("Viewport");


FDisplayClusterConfiguratorOutputMappingBuilder::FDisplayClusterConfiguratorOutputMappingBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
	UDisplayClusterConfigurationCluster* InConfigurationCluster,
	const TSharedRef<SDisplayClusterConfiguratorCanvasNode>& InCanvasNode)
	: ToolkitPtr(InToolkit)
	, CfgClusterPtr(InConfigurationCluster)
	, CanvasNodePtr(InCanvasNode)
{
	ConstraintCanvas = SNew(SDisplayClusterConfiguratorConstraintCanvas);

	EdGraph = CastChecked<UDisplayClusterConfiguratorGraph>(InCanvasNode->GetNodeObj()->GetGraph());
}

void FDisplayClusterConfiguratorOutputMappingBuilder::Build()
{
	CanvasSlot = MakeShared<FDisplayClusterConfiguratorOutputMappingCanvasSlot>(ToolkitPtr.Pin().ToSharedRef(), SharedThis(this), CfgClusterPtr.Get(), CanvasNodePtr.Pin().ToSharedRef());
	CanvasSlot->Build();
	Slots.Add(CanvasSlot);

	int32 WindowIndex = 0;
	for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& ClusterNodePair : CfgClusterPtr.Get()->Nodes)
	{
		TSharedPtr<FDisplayClusterConfiguratorOutputMappingWindowSlot> WindowSlot = MakeShared<FDisplayClusterConfiguratorOutputMappingWindowSlot>(ToolkitPtr.Pin().ToSharedRef(), SharedThis(this), CanvasSlot.ToSharedRef(), ClusterNodePair.Key, ClusterNodePair.Value, WindowIndex);
		WindowSlot->Build();
		Slots.Add(WindowSlot);
		CanvasSlot->AddChild(WindowSlot);

		for (const TPair<FString, UDisplayClusterConfigurationViewport*>& ViewportPair : ClusterNodePair.Value->Viewports)
		{
			TSharedPtr<FDisplayClusterConfiguratorOutputMappingViewportSlot> ViewportSlot = MakeShared<FDisplayClusterConfiguratorOutputMappingViewportSlot>(ToolkitPtr.Pin().ToSharedRef(), SharedThis(this), WindowSlot.ToSharedRef(), ViewportPair.Key, ViewportPair.Value);
			ViewportSlot->Build();
			Slots.Add(ViewportSlot);
			CanvasSlot->AddChild(ViewportSlot);
			WindowSlot->AddChild(ViewportSlot);
		}

		WindowIndex++;
	}
}

TSharedRef<SWidget> FDisplayClusterConfiguratorOutputMappingBuilder::GetCanvasWidget() const
{
	return ConstraintCanvas.ToSharedRef();
}

const TArray<TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>>& FDisplayClusterConfiguratorOutputMappingBuilder::GetAllSlots() const
{
	return Slots;
}

TSharedRef<SDisplayClusterConfiguratorConstraintCanvas> FDisplayClusterConfiguratorOutputMappingBuilder::GetConstraintCanvas() const
{
	return ConstraintCanvas.ToSharedRef();
}

UDisplayClusterConfiguratorGraph* FDisplayClusterConfiguratorOutputMappingBuilder::GetEdGraph()
{
	return EdGraph.Get();
}

TSharedPtr<SGraphPanel> FDisplayClusterConfiguratorOutputMappingBuilder::GetOwnerPanel() const
{
	return CanvasNodePtr.Pin()->GetOwnerPanel();
}

void FDisplayClusterConfiguratorOutputMappingBuilder::SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel)
{
	for (const TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>& Slot : Slots)
	{
		Slot->SetOwner(OwnerPanel);
	}
}

void FDisplayClusterConfiguratorOutputMappingBuilder::Tick(float InDeltaTime)
{
	for (const TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>& Slot : Slots)
	{
		Slot->Tick(InDeltaTime);
	}
}
