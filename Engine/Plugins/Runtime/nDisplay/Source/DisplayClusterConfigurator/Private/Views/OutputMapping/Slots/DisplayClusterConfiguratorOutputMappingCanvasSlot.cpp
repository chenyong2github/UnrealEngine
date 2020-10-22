// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/Slots/DisplayClusterConfiguratorOutputMappingCanvasSlot.h"


#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorStyle.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorOutputMappingBuilder.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorCanvasNode.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorConstraintCanvas.h"

#include "SGraphPanel.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorOutputMappingCanvasSlot"

uint32 const FDisplayClusterConfiguratorOutputMappingCanvasSlot::DefaultZOrder = 0;

FDisplayClusterConfiguratorOutputMappingCanvasSlot::FDisplayClusterConfiguratorOutputMappingCanvasSlot(
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
	const TSharedRef<FDisplayClusterConfiguratorOutputMappingBuilder>& InOutputMappingBuilder,
	UDisplayClusterConfigurationCluster* InConfigurationCluster,
	const TSharedRef<SDisplayClusterConfiguratorCanvasNode>& InCanvasNode)
	: ToolkitPtr(InToolkit)
	, OutputMappingBuilderPtr(InOutputMappingBuilder)
	, CfgClusterPtr(InConfigurationCluster)
	, CanvasNodePtr(InCanvasNode)
	, LocalSize(FVector2D(100.f))
	, LocalPosition(FVector2D(0.f))
	, ZOrder(DefaultZOrder)
	, CanvasScaleFactor(1.05f)
	, bIsActive(false)
{
}

void FDisplayClusterConfiguratorOutputMappingCanvasSlot::Build()
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingBuilder> OutputMappingBuilder = OutputMappingBuilderPtr.Pin();
	check(OutputMappingBuilder.IsValid());

	TAttribute<const FSlateBrush*> SelectedBrush = TAttribute<const FSlateBrush*>::Create(TAttribute<const FSlateBrush*>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetSelectedBrush));

	// Add Canvas wrap widget
	SAssignNew(Widget, SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(CanvasBox, SBox)
			.WidthOverride(LocalSize.X)
			.HeightOverride(LocalSize.Y)
			[
				SNew(SBorder)
				.BorderImage(SelectedBrush)
			]
		]

		+ SVerticalBox::Slot()
		.Padding(FMargin(2.f, 2.f))
		.AutoHeight()
		[
			SNew( SVerticalBox )

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Center)
			.AutoHeight()
			[
				SNew( SHorizontalBox )
											
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.f, 5.f, 5.f, 2.f)
				.HAlign(EHorizontalAlignment::HAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetCanvasSizeText)
					.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Regular"))
					.Justification(ETextJustify::Center)
				]
			]
		];

	Slot = &OutputMappingBuilder->GetConstraintCanvas()->AddSlot()
		.Anchors(FAnchors(0.f, 0.f, 0.f, 0.f))
		.Offset(FMargin(LocalPosition.X, LocalPosition.Y, 0.f, 0.f))
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(ZOrder)
		[
			Widget.ToSharedRef()
		];
}

void FDisplayClusterConfiguratorOutputMappingCanvasSlot::AddChild(TWeakPtr<IDisplayClusterConfiguratorOutputMappingSlot> InChildSlot)
{
	ChildrenSlots.Add(InChildSlot);
}

const FVector2D FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetLocalPosition() const
{
	return LocalPosition;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetLocalSize() const
{
	return LocalSize;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetConfigSize() const
{
	return RealSize;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetConfigPostion() const
{
	return RealPosition;
}

const FString& FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetName() const
{
	static FString SlotName = "Canvas";
	
	return SlotName;
}

const FName& FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetType() const
{
	return FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Canvas;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingCanvasSlot::CalculateClildLocalPosition(FVector2D InChildRealCoordinate) const
{
	// There are no calculation in Canvas now
	return InChildRealCoordinate;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingCanvasSlot::CalculateLocalPosition(FVector2D InChildRealPosition) const
{
	// There are no calculation in Canvas now
	return InChildRealPosition;
}

const FVector2D FDisplayClusterConfiguratorOutputMappingCanvasSlot::CalculateLocalSize(FVector2D InRealSize) const
{
	FVector2D Size = FVector2D::ZeroVector;
		
	if (RealSize.X > RealSize.Y)
	{
		Size.X = InRealSize.X * CanvasScaleFactor;
		Size.Y = InRealSize.Y + (Size.X - InRealSize.X);
	}
	else
	{
		Size.Y = InRealSize.Y * CanvasScaleFactor;
		Size.X = InRealSize.X + (Size.Y - InRealSize.Y);
	}

	return Size;
}

TSharedRef<SWidget> FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetWidget() const
{
	return Widget.ToSharedRef();
}

const FSlateBrush* FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetSelectedBrush() const
{
	TSharedPtr<SDisplayClusterConfiguratorCanvasNode> CanvasNode = CanvasNodePtr.Pin();
	check(CanvasNode.IsValid());

	if (CanvasNode->GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(CanvasNode->GetNodeObj()))
	{
		// Selected Case
		return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Selected.Canvas.Brush");
	}

	// Regular case
	return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Regular.Canvas.Brush");
}

FText FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetCanvasSizeText() const
{
	return FText::Format(LOCTEXT("CanvasResolution", "Canvas Resolution {0} x {1}"), FText::AsNumber(FMath::RoundToInt(RealSize.X)), FText::AsNumber(FMath::RoundToInt(RealSize.Y)));
}

uint32 FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetZOrder() const
{
	return ZOrder;
}

TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetParentSlot() const
{
	return TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>();
}

UObject* FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetEditingObject() const
{
	return CfgClusterPtr.Get();
}

UEdGraphNode* FDisplayClusterConfiguratorOutputMappingCanvasSlot::GetGraphNode() const
{
	return CanvasNodePtr.Pin()->GetNodeObj();
}

bool FDisplayClusterConfiguratorOutputMappingCanvasSlot::IsOutsideParent() const
{
	// Canvas is root slot
	return false;
}

bool FDisplayClusterConfiguratorOutputMappingCanvasSlot::IsOutsideParentBoundary() const
{
	// Canvas is root slot
	return false;
}

void FDisplayClusterConfiguratorOutputMappingCanvasSlot::Tick(float InDeltaTime)
{
	// Resize canvas slot
	FMargin CanvasMargin;
	FVector2D CanvasPosition = FVector2D::ZeroVector;

	const auto ComputeCanvasSize = [this](FMargin& OutCanvasMargin, const TSharedRef<IDisplayClusterConfiguratorOutputMappingSlot>& Child, uint32 Index)
	{
		FVector2D Position = Child->GetLocalPosition();
		FVector2D Size = Child->GetLocalSize();

		float PositionRight = Position.X + Size.X;
		float PositionBottom = Position.Y + Size.Y;
		
		// Set defauld position on index 0
		if (Index == 0)
		{
			OutCanvasMargin.Left = Position.X;
			OutCanvasMargin.Right = PositionRight;
			OutCanvasMargin.Top = Position.Y;
			OutCanvasMargin.Bottom = PositionBottom;
		}
		else
		{
			if (OutCanvasMargin.Left > Position.X)
			{
				OutCanvasMargin.Left = Position.X;
			}
			if (OutCanvasMargin.Right < PositionRight)
			{
				OutCanvasMargin.Right = PositionRight;
			}
			if (OutCanvasMargin.Top > Position.Y)
			{
				OutCanvasMargin.Top = Position.Y;
			}
			if (OutCanvasMargin.Bottom < PositionBottom)
			{
				OutCanvasMargin.Bottom = PositionBottom;
			}
		}
	};

	// Loop through all windows
	bool bIsAllWindowNodesZero = true;
	for (int32 ChildIndex = 0; ChildIndex < ChildrenSlots.Num(); ChildIndex++)
	{
		if (TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Child = ChildrenSlots[ChildIndex].Pin())
		{
			if (Child->GetType() == FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Window)
			{
				if (!Child->GetLocalSize().IsZero())
				{
					bIsAllWindowNodesZero = false;

					ComputeCanvasSize(CanvasMargin, Child.ToSharedRef(), ChildIndex);
				}
			}
		}
	}

	// Loop through all Viewport if all windows with size 0
	if (bIsAllWindowNodesZero)
	{
		for (int32 ChildIndex = 0; ChildIndex < ChildrenSlots.Num(); ChildIndex++)
		{
			if (TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot> Child = ChildrenSlots[ChildIndex].Pin())
			{
				if (Child->GetType() == FDisplayClusterConfiguratorOutputMappingBuilder::FSlot::Viewport)
				{
					ComputeCanvasSize(CanvasMargin, Child.ToSharedRef(), ChildIndex);
				}
			}
		}
	}

	// Update class properties
	RealSize = FVector2D(FMath::Abs(CanvasMargin.Right - CanvasMargin.Left), FMath::Abs(CanvasMargin.Bottom - CanvasMargin.Top));
	RealPosition = FVector2D(CanvasMargin.Left, CanvasMargin.Top);

	FVector2D CanvasSizeWithScale = CalculateLocalSize(RealSize);
	if (!CanvasBox->GetDesiredSize().Equals(CanvasSizeWithScale, SMALL_NUMBER))
	{
		SetLocalSize(CanvasSizeWithScale);
		SetLocalPosition(RealPosition -((CanvasSizeWithScale - RealSize) / 2.f));
	}
}

void FDisplayClusterConfiguratorOutputMappingCanvasSlot::SetActive(bool bActive)
{
	bIsActive = bActive;
}

void FDisplayClusterConfiguratorOutputMappingCanvasSlot::SetLocalPosition(FVector2D InLocalPosition)
{
	LocalPosition = InLocalPosition;

	FMargin Offset = FMargin(InLocalPosition.X, InLocalPosition.Y, 0.f, 0.f);
	Slot->Offset(Offset);
}

void FDisplayClusterConfiguratorOutputMappingCanvasSlot::SetLocalSize(FVector2D InLocalSize)
{
	LocalSize = InLocalSize;

	CanvasBox->SetWidthOverride(LocalSize.X);
	CanvasBox->SetHeightOverride(LocalSize.Y);
}

void FDisplayClusterConfiguratorOutputMappingCanvasSlot::SetZOrder(uint32 InZOrder)
{
	ZOrder = InZOrder;
}

#undef LOCTEXT_NAMESPACE