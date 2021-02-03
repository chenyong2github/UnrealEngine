// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorCanvasNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorWindowNode.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorStyle.h"

void UDisplayClusterConfiguratorWindowNode::Initialize(const FString& InNodeName, UDisplayClusterConfigurationClusterNode* InCfgNode, uint32 InWindowIndex, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	UDisplayClusterConfiguratorBaseNode::Initialize(InNodeName, InCfgNode, InToolkit);

	CornerColor = FDisplayClusterConfiguratorStyle::GetCornerColor(InWindowIndex);

	NodePosX = InCfgNode->WindowRect.X;
	NodePosY = InCfgNode->WindowRect.Y;
	NodeWidth = InCfgNode->WindowRect.W;
	NodeHeight = InCfgNode->WindowRect.H;

	InCfgNode->OnPostEditChangeChainProperty.Add(UDisplayClusterConfigurationViewport::FOnPostEditChangeChainProperty::FDelegate::CreateUObject(this, &UDisplayClusterConfiguratorWindowNode::OnPostEditChangeChainProperty));
}

TSharedPtr<SGraphNode> UDisplayClusterConfiguratorWindowNode::CreateVisualWidget()
{
	return SNew(SDisplayClusterConfiguratorWindowNode, this, ToolkitPtr.Pin().ToSharedRef());;
}

void UDisplayClusterConfiguratorWindowNode::UpdateObject()
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = GetObjectChecked<UDisplayClusterConfigurationClusterNode>();

	CfgClusterNode->WindowRect.X = NodePosX;
	CfgClusterNode->WindowRect.Y = NodePosY;
	CfgClusterNode->WindowRect.W = NodeWidth;
	CfgClusterNode->WindowRect.H = NodeHeight;
}

void UDisplayClusterConfiguratorWindowNode::OnNodeAligned(const FVector2D& PositionChange, bool bUpdateChildren)
{
	Super::OnNodeAligned(PositionChange);

	if (bUpdateChildren)
	{
		UpdateChildPositions(PositionChange);
	}
}

const FDisplayClusterConfigurationRectangle& UDisplayClusterConfiguratorWindowNode::GetCfgWindowRect() const
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = GetObjectChecked<UDisplayClusterConfigurationClusterNode>();
	return CfgClusterNode->WindowRect;
}

FString UDisplayClusterConfiguratorWindowNode::GetCfgHost() const
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = GetObjectChecked<UDisplayClusterConfigurationClusterNode>();
	return CfgClusterNode->Host;
}

bool UDisplayClusterConfiguratorWindowNode::IsFixedAspectRatio() const
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = GetObjectChecked<UDisplayClusterConfigurationClusterNode>();
	return CfgClusterNode->bFixedAspectRatio;
}

void UDisplayClusterConfiguratorWindowNode::SetParentCanvas(UDisplayClusterConfiguratorCanvasNode* InParentCanvas)
{
	ParentCanvas = InParentCanvas;
}

UDisplayClusterConfiguratorCanvasNode* UDisplayClusterConfiguratorWindowNode::GetParentCanvas() const
{
	if (ParentCanvas.IsValid())
	{
		return ParentCanvas.Get();
	}

	return nullptr;
}

void UDisplayClusterConfiguratorWindowNode::AddViewportNode(UDisplayClusterConfiguratorViewportNode* ViewportNode)
{
	ViewportNode->SetParentWindow(this);
	ChildViewports.Add(ViewportNode);
}

const TArray<UDisplayClusterConfiguratorViewportNode*>& UDisplayClusterConfiguratorWindowNode::GetChildViewports() const
{
	return ChildViewports;
}

void UDisplayClusterConfiguratorWindowNode::UpdateChildPositions(const FVector2D& Offset)
{
	for (TArray<UDisplayClusterConfiguratorViewportNode*>::TConstIterator NodeIt(ChildViewports); NodeIt; ++NodeIt)
	{
		UDisplayClusterConfiguratorViewportNode* Node = *NodeIt;
		Node->Modify();
		Node->NodePosX += Offset.X;
		Node->NodePosY += Offset.Y;
		Node->UpdateObject();
	}
}

FVector2D UDisplayClusterConfiguratorWindowNode::FindNonOverlappingOffsetFromParent(const FVector2D& InDesiredOffset)
{
	const UDisplayClusterConfiguratorCanvasNode* Parent = GetParentCanvas();
	check(Parent);

	FVector2D BestOffset = InDesiredOffset;

	for (TArray<UDisplayClusterConfiguratorWindowNode*>::TConstIterator NodeIt(Parent->GetChildWindows()); NodeIt; ++NodeIt)
	{
		UDisplayClusterConfiguratorWindowNode* ChildNode = *NodeIt;

		// Skip check against self
		if (this == ChildNode)
		{
			continue;
		}

		BestOffset = ChildNode->FindNonOverlappingOffset(this, BestOffset);

		// Break if we have hit a best offset of zero; there is no offset that can be performed that doesn't cause intersection.
		if (BestOffset.IsNearlyZero())
		{
			return FVector2D::ZeroVector;
		}
	}

	// In some cases, the node may still be intersecting with other nodes if its offset change pushed it into another node that it previously
	// checked. Do a final intersection check over all nodes and return zero offset if the calculated best offset still causes intersection.
	for (TArray<UDisplayClusterConfiguratorWindowNode*>::TConstIterator NodeIt(Parent->GetChildWindows()); NodeIt; ++NodeIt)
	{
		UDisplayClusterConfiguratorWindowNode* ChildNode = *NodeIt;

		// Skip check against self
		if (this == ChildNode)
		{
			continue;
		}

		if (ChildNode->WillOverlap(this, BestOffset))
		{
			return FVector2D::ZeroVector;
		}
	}

	return BestOffset;
}

FVector2D UDisplayClusterConfiguratorWindowNode::FindNonOverlappingSizeFromParent(const FVector2D& InDesiredSize, const bool bFixedApsectRatio)
{
	const UDisplayClusterConfiguratorCanvasNode* Parent = GetParentCanvas();
	check(Parent);

	FVector2D BestSize = InDesiredSize;
	const FVector2D NodeSize = GetNodeSize();

	// If desired size is smaller in both dimensions to the slot's current size, can return it immediately, as shrinking a slot can't cause any new intersections.
	if (BestSize - NodeSize < FVector2D::ZeroVector)
	{
		return BestSize;
	}

	for (TArray<UDisplayClusterConfiguratorWindowNode*>::TConstIterator NodeIt(Parent->GetChildWindows()); NodeIt; ++NodeIt)
	{
		UDisplayClusterConfiguratorWindowNode* ChildNode = *NodeIt;

		// Skip check against self
		if (this == ChildNode)
		{
			continue;
		}

		BestSize = ChildNode->FindNonOverlappingSize(this, BestSize, bFixedApsectRatio);

		// If the best size has shrunk to be equal to current size, simply return the current size, as there is no larger size
		// that doesn't cause intersection.
		if (BestSize.Equals(NodeSize))
		{
			break;
		}
	}

	return BestSize;
}

void UDisplayClusterConfiguratorWindowNode::OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent)
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = GetObjectChecked<UDisplayClusterConfigurationClusterNode>();

	const FName& PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, X) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, Y))
	{
		// Change slots and children position, config object already updated 
		FVector2D Offset = FVector2D(CfgClusterNode->WindowRect.X - NodePosX, CfgClusterNode->WindowRect.Y - NodePosY);
		NodePosX = CfgClusterNode->WindowRect.X;
		NodePosY = CfgClusterNode->WindowRect.Y;

		UpdateChildPositions(Offset);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, W) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, H))
	{
		// Change node slot size, config object already updated 
		NodeWidth = CfgClusterNode->WindowRect.W;
		NodeHeight = CfgClusterNode->WindowRect.H;
	}
}