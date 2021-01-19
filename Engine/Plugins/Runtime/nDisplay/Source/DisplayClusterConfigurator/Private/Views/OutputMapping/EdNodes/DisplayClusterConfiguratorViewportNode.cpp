// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorViewportNode.h"

#include "DisplayClusterConfigurationTypes.h"

void UDisplayClusterConfiguratorViewportNode::Initialize(const FString& InViewportName, UDisplayClusterConfigurationViewport* InCfgViewport, UDisplayClusterConfiguratorWindowNode* InParentWindow, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	UDisplayClusterConfiguratorBaseNode::Initialize(InViewportName, InCfgViewport, InToolkit);

	NodePosX = InCfgViewport->Region.X + InParentWindow->NodePosX;
	NodePosY = InCfgViewport->Region.Y + InParentWindow->NodePosY;
	NodeWidth = InCfgViewport->Region.W;
	NodeHeight = InCfgViewport->Region.H;

	InCfgViewport->OnPostEditChangeChainProperty.Add(UDisplayClusterConfigurationViewport::FOnPostEditChangeChainProperty::FDelegate::CreateUObject(this, &UDisplayClusterConfiguratorViewportNode::OnPostEditChangeChainProperty));
}

TSharedPtr<SGraphNode> UDisplayClusterConfiguratorViewportNode::CreateVisualWidget()
{
	return SNew(SDisplayClusterConfiguratorViewportNode, this, ToolkitPtr.Pin().ToSharedRef());;
}

void UDisplayClusterConfiguratorViewportNode::UpdateObject()
{
	const UDisplayClusterConfiguratorWindowNode* Parent = GetParentWindow();
	check(Parent);

	FVector2D ViewportLocalPosition = FVector2D(NodePosX - Parent->NodePosX, NodePosY - Parent->NodePosY);

	UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	CfgViewport->Region.X = ViewportLocalPosition.X;
	CfgViewport->Region.Y = ViewportLocalPosition.Y;
	CfgViewport->Region.W = NodeWidth;
	CfgViewport->Region.H = NodeHeight;
}

const FDisplayClusterConfigurationRectangle& UDisplayClusterConfiguratorViewportNode::GetCfgViewportRegion() const
{
	UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	return CfgViewport->Region;
}

bool UDisplayClusterConfiguratorViewportNode::IsFixedAspectRatio() const
{
	UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	return CfgViewport->bFixedAspectRatio;
}

void UDisplayClusterConfiguratorViewportNode::SetParentWindow(UDisplayClusterConfiguratorWindowNode* InParentWindow)
{
	ParentWindow = InParentWindow;
}

UDisplayClusterConfiguratorWindowNode* UDisplayClusterConfiguratorViewportNode::GetParentWindow() const
{
	if (ParentWindow.IsValid())
	{
		return ParentWindow.Get();
	}

	return nullptr;
}

void UDisplayClusterConfiguratorViewportNode::SetPreviewTexture(UTexture* InTexture)
{
	PreviewTexture = InTexture;
}

UTexture* UDisplayClusterConfiguratorViewportNode::GetPreviewTexture() const
{
	if (PreviewTexture.IsValid())
	{
		return PreviewTexture.Get();
	}

	return nullptr;
}

bool UDisplayClusterConfiguratorViewportNode::IsOutsideParent() const
{
	if (ParentWindow.IsValid())
	{
		FBox2D Bounds = GetNodeBounds();
		FBox2D ParentBounds = ParentWindow->GetNodeBounds();

		if (ParentBounds.GetSize().IsZero())
		{
			return false;
		}

		if (Bounds.Min.X > ParentBounds.Max.X || Bounds.Min.Y > ParentBounds.Max.Y)
		{
			return true;
		}

		if (Bounds.Max.X < ParentBounds.Min.X || Bounds.Max.Y < ParentBounds.Min.Y)
		{
			return true;
		}
	}

	return false;
}

bool UDisplayClusterConfiguratorViewportNode::IsOutsideParentBoundary() const
{
	if (ParentWindow.IsValid())
	{
		FBox2D Bounds = GetNodeBounds();
		FBox2D ParentBounds = ParentWindow->GetNodeBounds();

		if (ParentBounds.GetSize().IsZero())
		{
			return false;
		}

		if (Bounds.Min.X < ParentBounds.Min.X || Bounds.Min.Y < ParentBounds.Min.Y)
		{
			return true;
		}

		if (Bounds.Max.X > ParentBounds.Max.X || Bounds.Max.Y > ParentBounds.Max.Y)
		{
			return true;
		}
	}

	return false;	
}

FVector2D UDisplayClusterConfiguratorViewportNode::FindNonOverlappingOffsetFromParent(const FVector2D& InDesiredOffset)
{
	const UDisplayClusterConfiguratorWindowNode* Parent = GetParentWindow();
	check(Parent);

	FVector2D BestOffset = InDesiredOffset;

	for (TArray<UDisplayClusterConfiguratorViewportNode*>::TConstIterator NodeIt(Parent->GetChildViewports()); NodeIt; ++NodeIt)
	{
		UDisplayClusterConfiguratorViewportNode* ChildNode = *NodeIt;

		// Skip check against self
		if (this == ChildNode)
		{
			continue;
		}

		BestOffset = ChildNode->FindNonOverlappingOffset(this, BestOffset);

		// Break if we have hit a best offset of zero; there is no offset that can be performed that doesn't cause intersection.
		if (BestOffset.IsNearlyZero())
		{
			FVector2D::ZeroVector;
		}
	}

	// In some cases, the node may still be intersecting with other nodes if its offset change pushed it into another node that it previously
	// checked. Do a final intersection check over all nodes and return zero offset if the calculated best offset still causes intersection.
	for (TArray<UDisplayClusterConfiguratorViewportNode*>::TConstIterator NodeIt(Parent->GetChildViewports()); NodeIt; ++NodeIt)
	{
		UDisplayClusterConfiguratorViewportNode* ChildNode = *NodeIt;

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

FVector2D UDisplayClusterConfiguratorViewportNode::FindNonOverlappingSizeFromParent(const FVector2D& InDesiredSize, const bool bFixedApsectRatio)
{
	const UDisplayClusterConfiguratorWindowNode* Parent = GetParentWindow();
	check(Parent);

	FVector2D BestSize = InDesiredSize;
	const FVector2D NodeSize = GetNodeSize();

	// If desired size is smaller in both dimensions to the slot's current size, can return it immediately, as shrinking a slot can't cause any new intersections.
	if (BestSize - NodeSize < FVector2D::ZeroVector)
	{
		return BestSize;
	}

	for (TArray<UDisplayClusterConfiguratorViewportNode*>::TConstIterator NodeIt(Parent->GetChildViewports()); NodeIt; ++NodeIt)
	{
		UDisplayClusterConfiguratorViewportNode* ChildNode = *NodeIt;

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

void UDisplayClusterConfiguratorViewportNode::OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const UDisplayClusterConfiguratorWindowNode* Parent = GetParentWindow();
	check(Parent);

	UDisplayClusterConfigurationViewport* CfgViewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	const FName& PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, X) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, Y))
	{
		// Change slots and children position, config object already updated 
		NodePosX = CfgViewport->Region.X + Parent->NodePosX;
		NodePosY = CfgViewport->Region.Y + Parent->NodePosY;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, W) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, H))
	{
		// Change node slot size, config object already updated
		NodeWidth = CfgViewport->Region.W;
		NodeHeight = CfgViewport->Region.H;
	}
}