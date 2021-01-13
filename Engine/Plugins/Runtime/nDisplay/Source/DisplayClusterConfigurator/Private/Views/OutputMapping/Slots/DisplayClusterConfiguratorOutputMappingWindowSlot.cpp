// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/Slots/DisplayClusterConfiguratorOutputMappingWindowSlot.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorGraph.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorOutputMappingBuilder.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorConstraintCanvas.h"

#include "SGraphPanel.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorOutputMappingWindowSlot"

uint32 const FDisplayClusterConfiguratorOutputMappingWindowSlot::DefaultZOrder = 100;
uint32 const FDisplayClusterConfiguratorOutputMappingWindowSlot::ActiveZOrder = 101;
uint32 const FDisplayClusterConfiguratorOutputMappingWindowSlot::InfoSlotZOrderOffset = 200;
uint32 const FDisplayClusterConfiguratorOutputMappingWindowSlot::CornerImageSlotZOrderOffset = 201;

FDisplayClusterConfiguratorOutputMappingWindowSlot::FDisplayClusterConfiguratorOutputMappingWindowSlot(
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
	const TSharedRef<FDisplayClusterConfiguratorOutputMappingBuilder>& InOutputMappingBuilder,
	const TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot>& InParentSlot,
	const FString& InNodeName,
	UDisplayClusterConfigurationClusterNode* InConfigurationClusterNode,
	uint32 InWindowIndex)
	: ToolkitPtr(InToolkit)
	, OutputMappingBuilderPtr(InOutputMappingBuilder)
	, ParentSlotPtr(InParentSlot)
	, NodeName(InNodeName)
	, CfgClusterNodePtr(InConfigurationClusterNode)
	, WindowIndex(InWindowIndex)
	, LocalSize(FVector2D::ZeroVector)
	, LocalPosition(FVector2D::ZeroVector)
	, ZOrder(DefaultZOrder)
	, WindowXFactror(1.0f)
	, WindowYFactror(1.0f)
	, bIsActive(false)
{
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::Build()
{
	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedPtr<FDisplayClusterConfiguratorOutputMappingBuilder> OutputMappingBuilder = OutputMappingBuilderPtr.Pin();
	check(OutputMappingBuilder.IsValid());

	UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
	check(CfgClusterNode != nullptr);

	// Register Delegates
	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping  = Toolkit->GetViewOutputMapping();
	OutputMapping->RegisterOnShowWindowInfo(IDisplayClusterConfiguratorViewOutputMapping::FOnShowWindowInfoDelegate::CreateSP(this, &FDisplayClusterConfiguratorOutputMappingWindowSlot::OnShowWindowInfo));
	OutputMapping->RegisterOnShowWindowCornerImage(IDisplayClusterConfiguratorViewOutputMapping::FOnShowWindowCornerImageDelegate::CreateSP(this, &FDisplayClusterConfiguratorOutputMappingWindowSlot::OnShowWindowCornerImage));
	CfgClusterNode->OnPostEditChangeChainProperty.Add(UDisplayClusterConfigurationViewport::FOnPostEditChangeChainProperty::FDelegate::CreateSP(this, &FDisplayClusterConfiguratorOutputMappingWindowSlot::OnPostEditChangeChainProperty));

	TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Parent = GetParentSlot();

	// Update slot parameters
	LocalPosition = CalculateLocalPosition(FVector2D(CfgClusterNode->WindowRect.X, CfgClusterNode->WindowRect.Y));
	LocalSize = CalculateLocalSize(FVector2D(CfgClusterNode->WindowRect.W, CfgClusterNode->WindowRect.H));

	// Create Ed Node
	const UClass* WindowNodeClass = UDisplayClusterConfiguratorWindowNode::StaticClass();
	UDisplayClusterConfiguratorWindowNode* WindowNode = NewObject<UDisplayClusterConfiguratorWindowNode>(OutputMappingBuilder->GetEdGraph(), WindowNodeClass, NAME_None, RF_Transactional);
	EdGraphWindowNode = TStrongObjectPtr<UDisplayClusterConfiguratorWindowNode>(WindowNode);
	WindowNode->Initialize(CfgClusterNode, NodeName, SharedThis(this), ToolkitPtr.Pin().ToSharedRef());
	WindowNode->CreateNewGuid();
	WindowNode->PostPlacedNewNode();
	WindowNode->CornerColor = FDisplayClusterConfiguratorStyle::GetCornerColor(WindowIndex);

	// Create Graph Node
	NodeWidget = SNew(SDisplayClusterConfiguratorWindowNode, WindowNode, SharedThis(this), Toolkit.ToSharedRef());

	// Add Slot to Canvas
	NodeSlot = &OutputMappingBuilder->GetConstraintCanvas()->AddSlot()
		.Anchors(FAnchors(0.f, 0.f, 0.f, 0.f))
		.Offset(FMargin(LocalPosition.X, LocalPosition.Y, 0, 0))
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(ZOrder)
		[
			NodeWidget.ToSharedRef()
		];

	InfoWidget = NodeWidget->CreateInfoWidget();
	OnShowWindowInfo(OutputMapping->IsShowWindowInfo());
	InfoWidget->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorOutputMappingWindowSlot::GetInfoWidgetVisibility)));
	InfoSlot = &OutputMappingBuilder->GetConstraintCanvas()->AddSlot()
		.Anchors(FAnchors(0.f, 0.f, 0.f, 0.f))
		.Offset(FMargin(LocalPosition.X, LocalPosition.Y, 0, 0))
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(ZOrder + InfoSlotZOrderOffset)
		[
			InfoWidget.ToSharedRef()
		];

	CornerImageWidget = NodeWidget->GetCornerImageWidget();
	OnShowWindowCornerImage(OutputMapping->IsShowWindowCornerImage());
	CornerImageWidget->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorOutputMappingWindowSlot::GetCornerImageVisibility)));
	CornerImageSlot = &OutputMappingBuilder->GetConstraintCanvas()->AddSlot()
		.Anchors(FAnchors(0.f, 0.f, 0.f, 0.f))
		.Offset(FMargin(LocalPosition.X, LocalPosition.Y, 0, 0))
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(ZOrder + CornerImageSlotZOrderOffset)
		[
			CornerImageWidget.ToSharedRef()
		];
	}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::AddChild(TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> InChildSlot)
{
	ChildrenSlots.Add(InChildSlot);
}

const FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::GetConfigSize() const
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
	check(CfgClusterNode != nullptr);

	return FVector2D(CfgClusterNode->WindowRect.W, CfgClusterNode->WindowRect.H);
}

const FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::GetLocalPosition() const
{
	return LocalPosition;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::GetLocalSize() const
{
	return LocalSize;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::GetConfigPostion() const
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
	check(CfgClusterNode != nullptr);

	return FVector2D(CfgClusterNode->WindowRect.X, CfgClusterNode->WindowRect.Y);
}

const FString& FDisplayClusterConfiguratorOutputMappingWindowSlot::GetName() const
{
	return NodeName;
}

const FName& FDisplayClusterConfiguratorOutputMappingWindowSlot::GetType() const
{
	return FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Window;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::CalculateClildLocalPosition(FVector2D RealCoordinate) const
{
	return FVector2D(LocalPosition.X + RealCoordinate.X, LocalPosition.Y + RealCoordinate.Y);
}

const FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::CalculateLocalPosition(FVector2D RealCoordinate) const
{
	return FVector2D(RealCoordinate.X * WindowXFactror, RealCoordinate.Y * WindowYFactror);
}

const FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::CalculateLocalSize(FVector2D RealSize) const
{
	return FVector2D(RealSize.X * WindowXFactror, RealSize.Y * WindowYFactror);
}

TSharedRef<SWidget> FDisplayClusterConfiguratorOutputMappingWindowSlot::GetWidget() const
{
	return NodeWidget.ToSharedRef();
}

uint32 FDisplayClusterConfiguratorOutputMappingWindowSlot::GetZOrder() const
{
	return ZOrder;
}

TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> FDisplayClusterConfiguratorOutputMappingWindowSlot::GetParentSlot() const
{
	return ParentSlotPtr.Pin();
}

UObject* FDisplayClusterConfiguratorOutputMappingWindowSlot::GetEditingObject() const
{
	return CfgClusterNodePtr.Get();
}

bool FDisplayClusterConfiguratorOutputMappingWindowSlot::IsOutsideParent() const
{
	// Right now we keep all window inside the canvas
	return false;
}

bool FDisplayClusterConfiguratorOutputMappingWindowSlot::IsOutsideParentBoundary() const
{
	// Right now we keep all window inside the canvas
	return false;
}

UEdGraphNode* FDisplayClusterConfiguratorOutputMappingWindowSlot::GetGraphNode() const
{
	return EdGraphWindowNode.Get();
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::SetZOrder(uint32 InZOrder)
{
	ZOrder = InZOrder;

	NodeSlot->ZOrder(ZOrder);
	InfoSlot->ZOrder(ZOrder + InfoSlotZOrderOffset);
	CornerImageSlot->ZOrder(ZOrder + CornerImageSlotZOrderOffset);
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::OnShowWindowInfo(bool IsShow)
{
	bShowWindowInfoVisible = IsShow;
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::OnShowWindowCornerImage(bool IsShow)
{
	bShowWindowCornerImageVisible = IsShow;
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName& PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, X) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, Y)
		)
	{
		// Change slots and children position, config object already updated 
		UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
		check(CfgClusterNode != nullptr);



		FVector2D NewPosition = CalculateLocalPosition(FVector2D(CfgClusterNode->WindowRect.X, CfgClusterNode->WindowRect.Y));
		FVector2D DelatPosition = NewPosition - LocalPosition;
		LocalPosition = NewPosition;
		UpdateSlotsPositionInternal(LocalPosition, DelatPosition);

	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, W) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, H)
		)
	{
		// Change node slot size, config object already updated 
		UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
		check(CfgClusterNode != nullptr);

		LocalSize = CalculateLocalSize(FVector2D(CfgClusterNode->WindowRect.W, CfgClusterNode->WindowRect.H));
		NodeWidget->SetNodeSize(LocalSize, false);
	}
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::UpdateSlotsPositionInternal(FVector2D InLocalPosition, FVector2D InDeltaPosition)
{
	// Update Slot postion
	NodeSlot->Offset(FMargin(InLocalPosition.X, InLocalPosition.Y, 0.f, 0.f));
	InfoSlot->Offset(FMargin(InLocalPosition.X, InLocalPosition.Y, 0.f, 0.f));
	CornerImageSlot->Offset(FMargin(InLocalPosition.X, InLocalPosition.Y, 0.f, 0.f));

	// Update children
	for (TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> ChildPtr : ChildrenSlots)
	{
		TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Child = ChildPtr.Pin();
		Child->SetLocalPosition(Child->GetLocalPosition() + InDeltaPosition);
	}
}

EVisibility FDisplayClusterConfiguratorOutputMappingWindowSlot::GetInfoWidgetVisibility() const
{
	if (NodeWidget->IsNodeVisible() && 
		bShowWindowInfoVisible == true &&
		LocalSize.X > 0 &&
		LocalSize.Y > 0
		)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

EVisibility FDisplayClusterConfiguratorOutputMappingWindowSlot::GetCornerImageVisibility() const
{
	if (NodeWidget->IsNodeVisible() && 
		bShowWindowCornerImageVisible == true &&
		LocalSize.X > 0 &&
		LocalSize.Y > 0
		)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::SetActive(bool bActive)
{
	bIsActive = bActive;

	if (bActive)
	{
		SetZOrder(ActiveZOrder);
	}
	else
	{
		SetZOrder(DefaultZOrder);
	}
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::SetOwner(TSharedRef<SGraphPanel> InGraphPanel)
{
	NodeWidget->SetOwner(InGraphPanel);
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::SetLocalPosition(FVector2D InLocalPosition)
{
	if (!InLocalPosition.Equals(LocalPosition, SMALL_NUMBER))
	{
		FVector2D DelatPosition = InLocalPosition - LocalPosition;
		LocalPosition = InLocalPosition;
		SetConfigPosition(LocalPosition);
		UpdateSlotsPositionInternal(LocalPosition, DelatPosition);
	}
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::SetLocalSize(FVector2D InLocalSize)
{
	if (!InLocalSize.Equals(LocalSize, SMALL_NUMBER))
	{
		LocalSize = InLocalSize;
		SetConfigSize(LocalSize);
	}
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::SetConfigPosition(FVector2D InConfigPosition)
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
	check(CfgClusterNode != nullptr);

	CfgClusterNode->WindowRect.X = InConfigPosition.X;
	CfgClusterNode->WindowRect.Y = InConfigPosition.Y;
}

void FDisplayClusterConfiguratorOutputMappingWindowSlot::SetConfigSize(FVector2D InConfigSize)
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
	check(CfgClusterNode != nullptr);

	CfgClusterNode->WindowRect.W = InConfigSize.X;
	CfgClusterNode->WindowRect.H = InConfigSize.Y;
}

FORCEINLINE bool Intrudes(const FBox2D& BoxA, const FBox2D& BoxB)
{
	// Similar to FBox2D::Intersects, but ignores the case where the box edges are touching.

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

FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::FindNonOverlappingOffset(TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> InSlot, const FVector2D& InDesiredOffset)
{
	FVector2D BestOffset = InDesiredOffset;

	if (TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Slot = InSlot.Pin())
	{
		const FBox2D OriginalSlotBounds = FBox2D(Slot->GetLocalPosition(), Slot->GetLocalPosition() + Slot->GetLocalSize());
		FBox2D SlotBounds = OriginalSlotBounds.ShiftBy(BestOffset);
		FVector2D SlotCenter = SlotBounds.GetCenter();

		for (int32 ChildIndex = 0; ChildIndex < ChildrenSlots.Num(); ChildIndex++)
		{
			// Skip check against self
			if (InSlot == ChildrenSlots[ChildIndex])
			{
				continue;
			}

			// Break if we have hit a best offset of zero; there is no offset that can be performed that doesn't cause intersection.
			if (BestOffset.IsNearlyZero())
			{
				break;
			}

			if (TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Child = ChildrenSlots[ChildIndex].Pin())
			{
				if (Child->GetType() == FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Viewport && !Child->GetLocalSize().IsZero())
				{
					const FBox2D ChildBounds = FBox2D(Child->GetLocalPosition(), Child->GetLocalPosition() + Child->GetLocalSize());
					const FVector2D ChildCenter = ChildBounds.GetCenter();

					if (Intrudes(SlotBounds, ChildBounds))
					{
						// If there is an intersection, we want to modify the desired offset in such a way that the new offset leaves the slot outside of the child's bounds.
						// Best way to do this is to move the slot either along the x or y axis a distance equal to the penetration depth along that axis. We must pick the 
						// axis that results in a new offset that is smaller than the desired one.
						const FBox2D IntersectionBox = FBox2D(FVector2D::Max(SlotBounds.Min, ChildBounds.Min), FVector2D::Min(SlotBounds.Max, ChildBounds.Max));
						const FVector2D AxisDepths = IntersectionBox.GetSize();

						// The direction determines which way we need to move the slot to avoid intersection, which is always in the direction that moves the slot bound's
						// center away from the child bound's center.
						const FVector2D Direction = FVector2D(FMath::Sign(SlotCenter.X - ChildCenter.X), FMath::Sign(SlotCenter.Y - ChildCenter.Y));
						const FVector2D XShift = BestOffset + Direction.X * FVector2D(AxisDepths.X, 0);
						const FVector2D YShift = BestOffset + Direction.Y * FVector2D(0, AxisDepths.Y);

						BestOffset = XShift.Size() < YShift.Size() ? XShift : YShift;
						SlotBounds = OriginalSlotBounds.ShiftBy(BestOffset);
						SlotCenter = SlotBounds.GetCenter();
					}
				}
			}
		}
	}

	return BestOffset;
}

FVector2D FDisplayClusterConfiguratorOutputMappingWindowSlot::FindNonOverlappingSize(TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> InSlot, const FVector2D& InDesiredSize, const bool bFixedApsectRatio)
{
	FVector2D BestSize = InDesiredSize;

	if (TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Slot = InSlot.Pin())
	{
		const FVector2D OriginalSlotSize = Slot->GetLocalSize();
		const float AspectRatio = OriginalSlotSize.X / OriginalSlotSize.Y;
		FVector2D SizeChange = BestSize - OriginalSlotSize;

		// If desired size is smaller in both dimensions to the slot's current size, can return it immediately, as shrinking a slot can't cause any new intersections.
		if (SizeChange < FVector2D::ZeroVector)
		{
			return BestSize;
		}

		FBox2D SlotBounds = FBox2D(Slot->GetLocalPosition(), Slot->GetLocalPosition() + InDesiredSize);

		for (int32 ChildIndex = 0; ChildIndex < ChildrenSlots.Num(); ChildIndex++)
		{
			// Skip check against self
			if (InSlot == ChildrenSlots[ChildIndex])
			{
				continue;
			}

			// If the best size has shrunk to be less than or euqal to current size, simply return the current size, as there is no larger size
			// that doesn't cause intersection.
			if (SizeChange.IsNearlyZero())
			{
				break;
			}

			if (TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Child = ChildrenSlots[ChildIndex].Pin())
			{
				if (Child->GetType() == FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Viewport && !Child->GetLocalSize().IsZero())
				{
					FBox2D ChildBounds = FBox2D(Child->GetLocalPosition(), Child->GetLocalPosition() + Child->GetLocalSize());

					if (Intrudes(SlotBounds, ChildBounds))
					{
						// If there is an intersection, we want to modify the desired size in such a way that the new size leaves the slot outside of the child's bounds.
						// Best way to do this is to change the slot size along the x or y axis a distance equal to the penetration depth along that axis. We must pick the 
						// axis that results in a new size change that is smaller than the desired one. In the case where aspect ratio is fixed, we need to shift the other
						// axis as well a proportional amount.
						const FBox2D IntersectionBox = FBox2D(ChildBounds.Min, SlotBounds.Max);
						const FVector2D AxisDepths = IntersectionBox.GetSize();

						const FVector2D XShift = SizeChange - FVector2D(AxisDepths.X, bFixedApsectRatio ? AxisDepths.X / AspectRatio : 0);
						const FVector2D YShift = SizeChange - FVector2D(bFixedApsectRatio ? AxisDepths.Y * AspectRatio : 0, AxisDepths.Y);

						SizeChange = XShift.Size() < YShift.Size() ? XShift : YShift;
						BestSize = OriginalSlotSize + SizeChange;
						SlotBounds.Max = Slot->GetLocalPosition() + BestSize;
					}
				}
			}
		}
	}

	return BestSize;
}

#undef LOCTEXT_NAMESPACE