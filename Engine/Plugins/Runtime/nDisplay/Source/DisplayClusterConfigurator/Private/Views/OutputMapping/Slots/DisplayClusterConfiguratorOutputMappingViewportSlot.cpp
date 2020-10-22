// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/Slots/DisplayClusterConfiguratorOutputMappingViewportSlot.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorGraph.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorOutputMappingBuilder.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorConstraintCanvas.h"

#include "SGraphPanel.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorOutputMappingViewportSlot"

uint32 const FDisplayClusterConfiguratorOutputMappingViewportSlot::DefaultZOrder = 200;
uint32 const FDisplayClusterConfiguratorOutputMappingViewportSlot::ActiveZOrder = 201;

FDisplayClusterConfiguratorOutputMappingViewportSlot::FDisplayClusterConfiguratorOutputMappingViewportSlot(
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
	const TSharedRef<FDisplayClusterConfiguratorOutputMappingBuilder>& InOutputMappingBuilder,
	const TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot>& InParentSlot,
	const FString& InViewportName,
	UDisplayClusterConfigurationViewport* InConfigurationViewport)
	: ToolkitPtr(InToolkit)
	, OutputMappingBuilderPtr(InOutputMappingBuilder)
	, ParentSlotPtr(InParentSlot)
	, ViewportName(InViewportName)
	, CfgViewportPtr(InConfigurationViewport)
	, LocalSize(FVector2D::ZeroVector)
	, LocalPosition(FVector2D::ZeroVector)
	, ZOrder(DefaultZOrder)
	, bIsActive(false)
{
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::Build()
{
	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedPtr<FDisplayClusterConfiguratorOutputMappingBuilder> OutputMappingBuilder = OutputMappingBuilderPtr.Pin();
	check(OutputMappingBuilder.IsValid());

	UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
	check(CfgViewport != nullptr);

	TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> ParentSlot = ParentSlotPtr.Pin();
	check(ParentSlot != nullptr);

	// Register Delegates
	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	OutputMapping->RegisterOnShowOutsideViewports(IDisplayClusterConfiguratorViewOutputMapping::FOnShowOutsideViewportsDelegate::CreateSP(this, &FDisplayClusterConfiguratorOutputMappingViewportSlot::OnShowOutsideViewports));
	CfgViewport->OnPostEditChangeChainProperty.Add(UDisplayClusterConfigurationViewport::FOnPostEditChangeChainProperty::FDelegate::CreateSP(this, &FDisplayClusterConfiguratorOutputMappingViewportSlot::OnPostEditChangeChainProperty));


	// Update slot parameters
	LocalPosition = CalculateLocalPosition(FVector2D(CfgViewport->Region.X, CfgViewport->Region.Y));
	LocalSize = CalculateLocalSize(FVector2D(CfgViewport->Region.W, CfgViewport->Region.H));

	// Create Ed Node
	const UClass* ViewportNodeClass = UDisplayClusterConfiguratorViewportNode::StaticClass();
	UDisplayClusterConfiguratorViewportNode* ViewportNode = NewObject<UDisplayClusterConfiguratorViewportNode>(OutputMappingBuilder->GetEdGraph(), ViewportNodeClass, NAME_None, RF_Transactional);
	EdGraphViewportNode = TStrongObjectPtr<UDisplayClusterConfiguratorViewportNode>(ViewportNode);
	ViewportNode->Initialize(CfgViewport, ViewportName, SharedThis(this), ToolkitPtr.Pin().ToSharedRef());
	ViewportNode->CreateNewGuid();
	ViewportNode->PostPlacedNewNode();

	// Create Graph Node
	NodeWidget = SNew(SDisplayClusterConfiguratorViewportNode, ViewportNode, SharedThis(this), ToolkitPtr.Pin().ToSharedRef());
	OnShowOutsideViewports(OutputMapping->IsShowOutsideViewports());

	// Add Slot to Canvas
	NodeSlot = &OutputMappingBuilder->GetConstraintCanvas()->AddSlot()
		.Anchors(FAnchors(0.f, 0.f, 0.f, 0.f))
		.Offset(FMargin(LocalPosition.X, LocalPosition.Y, 0.f, 0.f))
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(ZOrder)
		[
			NodeWidget.ToSharedRef()
		];
}

const FVector2D FDisplayClusterConfiguratorOutputMappingViewportSlot::GetLocalPosition() const
{
	return LocalPosition;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingViewportSlot::GetLocalSize() const
{
	return LocalSize;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingViewportSlot::GetConfigSize() const
{
	UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
	check(CfgViewport != nullptr);

	return FVector2D(CfgViewport->Region.W, CfgViewport->Region.H);
}

const FVector2D FDisplayClusterConfiguratorOutputMappingViewportSlot::GetConfigPostion() const
{
	UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
	check(CfgViewport != nullptr);

	return FVector2D(CfgViewport->Region.X, CfgViewport->Region.Y);
}

const FString& FDisplayClusterConfiguratorOutputMappingViewportSlot::GetName() const
{
	return ViewportName;
}

const FName& FDisplayClusterConfiguratorOutputMappingViewportSlot::GetType() const
{
	return FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Viewport;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingViewportSlot::CalculateClildLocalPosition(FVector2D RealCoordinate) const
{
	// No extra calculation in Viewport Node
	return RealCoordinate;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingViewportSlot::CalculateLocalPosition(FVector2D RealCoordinate) const
{
	TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> ParentSlot = ParentSlotPtr.Pin();
	check(ParentSlot.IsValid());

	return ParentSlot->CalculateClildLocalPosition(RealCoordinate);
}

const FVector2D FDisplayClusterConfiguratorOutputMappingViewportSlot::CalculateLocalSize(FVector2D RealSize) const
{
	return RealSize;
}

TSharedRef<SWidget> FDisplayClusterConfiguratorOutputMappingViewportSlot::GetWidget() const
{
	return NodeWidget.ToSharedRef();
}

uint32 FDisplayClusterConfiguratorOutputMappingViewportSlot::GetZOrder() const
{
	return ZOrder;
}

TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> FDisplayClusterConfiguratorOutputMappingViewportSlot::GetParentSlot() const
{
	return ParentSlotPtr.Pin();
}

UObject* FDisplayClusterConfiguratorOutputMappingViewportSlot::GetEditingObject() const
{
	return CfgViewportPtr.Get();
}

bool FDisplayClusterConfiguratorOutputMappingViewportSlot::IsOutsideParent() const
{
	TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Parent = GetParentSlot();
	FVector2D ParentLocalPosition = Parent->GetLocalPosition();
	FVector2D ParentLocalSize = Parent->GetLocalSize();
	FVector2D Position = GetLocalPosition();
	FVector2D Size = GetLocalSize();

	// if parent size is 0 return false
	if (ParentLocalSize.IsZero())
	{
		return false;
	}

	if (Position.X > (ParentLocalPosition.X + ParentLocalSize.X) ||
		Position.Y > (ParentLocalPosition.Y + ParentLocalSize.Y))
	{
		return true;
	}

	if ((Position.X + Size.X) < ParentLocalPosition.X ||
		(Position.Y + Size.Y) < ParentLocalPosition.Y)
	{
		return true;
	}

	return false;
}

bool FDisplayClusterConfiguratorOutputMappingViewportSlot::IsOutsideParentBoundary() const
{
	TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Parent = GetParentSlot();
	FVector2D ParentLocalPosition = Parent->GetLocalPosition();
	FVector2D ParentLocalSize = Parent->GetLocalSize();
	FVector2D Position = GetLocalPosition();
	FVector2D Size = GetLocalSize();

	// if parent size is 0 return false
	if (ParentLocalSize.IsZero())
	{
		return false;
	}

	if ((Position.X + Size.X) > (ParentLocalPosition.X + ParentLocalSize.X) ||
		(Position.Y + Size.Y) > (ParentLocalPosition.Y + ParentLocalSize.Y))
	{
		return true;
	}

	if (Position.X < ParentLocalPosition.X ||
		Position.Y < ParentLocalPosition.Y)
	{
		return true;
	}

	return false;
}

UEdGraphNode* FDisplayClusterConfiguratorOutputMappingViewportSlot::GetGraphNode() const
{
	return EdGraphViewportNode.Get();
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::SetConfigSize(FVector2D InConfigSize)
{
	UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
	check(CfgViewport != nullptr);

	CfgViewport->Region.W = InConfigSize.X;
	CfgViewport->Region.H = InConfigSize.Y;
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::SetZOrder(uint32 InZOrder)
{
	ZOrder = InZOrder;

	NodeSlot->ZOrder(ZOrder);
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::SetPreviewTexture(UTexture* InTexture)
{
	if (InTexture != nullptr)
	{
		NodeWidget->SetBackgroundBrushFromTexture(InTexture);
	}
	else
	{
		NodeWidget->SetBackgroundDefaultBrush();
	}
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::OnShowOutsideViewports(bool bShow)
{
	EVisibility Visibility = EVisibility::Visible;
	if (bShow == false)
	{
		if (IsOutsideParent())
		{
			Visibility = EVisibility::Hidden;
		}
	}

	NodeWidget->SetVisibility(Visibility);
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::OnPostEditChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName& PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, X) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, Y)
		)
	{
		// Change slot position, config object already updated 
		UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
		check(CfgViewport != nullptr);

		LocalPosition = CalculateLocalPosition(FVector2D(CfgViewport->Region.X, CfgViewport->Region.Y));
		NodeSlot->Offset(FMargin(LocalPosition.X, LocalPosition.Y, 0.f, 0.f));
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, W) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationRectangle, H)
		)
	{
		// Change slot size, config object already updated 
		UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
		check(CfgViewport != nullptr);

		LocalSize = CalculateLocalSize(FVector2D(CfgViewport->Region.W, CfgViewport->Region.H));
		NodeWidget->SetNodeSize(LocalSize);
	}
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::SetActive(bool bActive)
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

void FDisplayClusterConfiguratorOutputMappingViewportSlot::SetOwner(TSharedRef<SGraphPanel> InGraphPanel)
{
	NodeWidget->SetOwner(InGraphPanel);
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::SetLocalPosition(FVector2D InLocalPosition)
{
	if (!InLocalPosition.Equals(LocalPosition, SMALL_NUMBER))
	{
		LocalPosition = InLocalPosition;

		TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Parent = GetParentSlot();
		check(Parent.IsValid());

		FVector2D ParentLocalPosition = Parent->GetLocalPosition();

		SetConfigPosition(FVector2D(LocalPosition.X - ParentLocalPosition.X, LocalPosition.Y - ParentLocalPosition.Y));
		NodeSlot->Offset(FMargin(LocalPosition.X, LocalPosition.Y, 0.f, 0.f));
	}
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::SetLocalSize(FVector2D InLocalSize)
{
	if (!InLocalSize.Equals(LocalSize, SMALL_NUMBER))
	{
		LocalSize = InLocalSize;

		SetConfigSize(LocalSize);
	}
}

void FDisplayClusterConfiguratorOutputMappingViewportSlot::SetConfigPosition(FVector2D InConfigPosition)
{
	UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
	check(CfgViewport != nullptr);

	CfgViewport->Region.X = InConfigPosition.X;
	CfgViewport->Region.Y = InConfigPosition.Y;
}

#undef LOCTEXT_NAMESPACE