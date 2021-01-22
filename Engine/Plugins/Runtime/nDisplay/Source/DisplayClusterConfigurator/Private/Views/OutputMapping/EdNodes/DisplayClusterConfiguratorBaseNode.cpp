// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorToolkit.h"

void UDisplayClusterConfiguratorBaseNode::Initialize(const FString& InNodeName, UObject* InObject, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	ObjectToEdit = InObject;
	NodeName = InNodeName;
	ToolkitPtr = InToolkit;
}

void UDisplayClusterConfiguratorBaseNode::PostEditUndo()
{
	Super::PostEditUndo();
	
	UpdateObject();
}

void UDisplayClusterConfiguratorBaseNode::ResizeNode(const FVector2D& NewSize)
{
	NodeHeight = NewSize.Y;
	NodeWidth = NewSize.X;

	UpdateObject();
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

FBox2D UDisplayClusterConfiguratorBaseNode::GetNodeBounds() const
{
	FVector2D Min = FVector2D(NodePosX, NodePosY);
	FVector2D Max = Min + FVector2D(NodeWidth, NodeHeight);
	return FBox2D(Min, Max);
}

FVector2D UDisplayClusterConfiguratorBaseNode::GetNodePosition() const
{
	return FVector2D(NodePosX, NodePosY);
}

FVector2D UDisplayClusterConfiguratorBaseNode::GetNodeSize() const
{
	return FVector2D(NodeWidth, NodeHeight);
}

void UDisplayClusterConfiguratorBaseNode::OnNodeAligned(const FVector2D& PositionChange, bool bUpdateChildren)
{
	UpdateObject();
}

namespace
{
	bool Intrudes(FBox2D BoxA, FBox2D BoxB)
	{
		// Similar to FBox2D::Intersects, but ignores the case where the box edges are touching.

		// Special case if both boxes are directly on top of each other, which is considered an intrusion.
		if (BoxA == BoxB)
		{
			return true;
		}

		if ((BoxA.Min.X >= BoxB.Max.X) || (BoxB.Min.X >= BoxA.Max.X))
		{
			return false;
		}

		if ((BoxA.Min.Y >= BoxB.Max.Y) || (BoxB.Min.Y >= BoxA.Max.Y))
		{
			return false;
		}

		return true;
	}
}

bool UDisplayClusterConfiguratorBaseNode::WillOverlap(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredOffset, const FVector2D& InDesiredSizeChange) const
{
	const FBox2D NodeBounds = FBox2D(InNode->GetNodePosition() + InDesiredOffset, InNode->GetNodePosition() + InNode->GetNodeSize() + InDesiredOffset + InDesiredSizeChange);
	const FBox2D Bounds = GetNodeBounds();

	return Intrudes(NodeBounds, Bounds);
}

FVector2D UDisplayClusterConfiguratorBaseNode::FindNonOverlappingOffset(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredOffset) const
{
	FVector2D BestOffset = InDesiredOffset;

	const FBox2D NodeBounds = InNode->GetNodeBounds().ShiftBy(BestOffset);
	const FVector2D NodeCenter = NodeBounds.GetCenter();

	const FBox2D Bounds = GetNodeBounds();
	const FVector2D Center = Bounds.GetCenter();

	if (Intrudes(NodeBounds, Bounds))
	{
		// If there is an intersection, we want to modify the desired offset in such a way that the new offset leaves the slot outside of the child's bounds.
		// Best way to do this is to move the slot either along the x or y axis a distance equal to the penetration depth along that axis. We must pick the 
		// axis that results in a new offset that is smaller than the desired one.
		const FBox2D IntersectionBox = FBox2D(FVector2D::Max(NodeBounds.Min, Bounds.Min), FVector2D::Min(NodeBounds.Max, Bounds.Max));
		const FVector2D AxisDepths = IntersectionBox.GetSize();

		// The direction determines which way we need to move the slot to avoid intersection, which is always in the direction that moves the slot bound's
		// center away from the child bound's center.
		const FVector2D Direction = FVector2D(FMath::Sign(NodeCenter.X - Center.X), FMath::Sign(NodeCenter.Y - Center.Y));
		const FVector2D XShift = BestOffset + Direction.X * FVector2D(AxisDepths.X, 0);
		const FVector2D YShift = BestOffset + Direction.Y * FVector2D(0, AxisDepths.Y);

		BestOffset = XShift.Size() < YShift.Size() ? XShift : YShift;
	}

	return BestOffset;
}

FVector2D UDisplayClusterConfiguratorBaseNode::FindNonOverlappingSize(UDisplayClusterConfiguratorBaseNode* InNode, const FVector2D& InDesiredSize, const bool bFixedApsectRatio) const
{
	FVector2D BestSize = InDesiredSize;

	const FVector2D OriginalSlotSize = InNode->GetNodeSize();
	const float AspectRatio = OriginalSlotSize.X / OriginalSlotSize.Y;
	FVector2D SizeChange = BestSize - OriginalSlotSize;

	if (SizeChange < FVector2D::ZeroVector)
	{
		return BestSize;
	}

	const FBox2D NodeBounds = FBox2D(InNode->GetNodePosition(), InNode->GetNodePosition() + InDesiredSize);
	const FBox2D Bounds = GetNodeBounds();

	if (Intrudes(NodeBounds, Bounds))
	{
		// If there is an intersection, we want to modify the desired size in such a way that the new size leaves the slot outside of the child's bounds.
		// Best way to do this is to change the slot size along the x or y axis a distance equal to the penetration depth along that axis. We must pick the 
		// axis that results in a new size change that is smaller than the desired one. In the case where aspect ratio is fixed, we need to shift the other
		// axis as well a proportional amount.
		const FBox2D IntersectionBox = FBox2D(Bounds.Min, NodeBounds.Max);
		const FVector2D AxisDepths = IntersectionBox.GetSize();

		const FVector2D XShift = SizeChange - FVector2D(AxisDepths.X, bFixedApsectRatio ? AxisDepths.X / AspectRatio : 0);
		const FVector2D YShift = SizeChange - FVector2D(bFixedApsectRatio ? AxisDepths.Y * AspectRatio : 0, AxisDepths.Y);

		SizeChange = XShift.Size() < YShift.Size() ? XShift : YShift;
		BestSize = OriginalSlotSize + SizeChange;
	}

	return BestSize;
}