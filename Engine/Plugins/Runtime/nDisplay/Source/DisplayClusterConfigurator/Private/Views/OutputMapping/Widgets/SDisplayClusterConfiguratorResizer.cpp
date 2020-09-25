// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorResizer.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

#include "Widgets/Images/SImage.h"
#include "SGraphPanel.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorResizer"

void SDisplayClusterConfiguratorResizer::Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<SDisplayClusterConfiguratorBaseNode>& InBaseNode)
{
	ToolkitPtr = InToolkit;
	BaseNodePtr = InBaseNode;
	bResizing = false;

	SetCursor(EMouseCursor::GrabHandClosed);

	ChildSlot
		[
			SNew(SImage)
			.Image(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.OutputMapping.ResizeAreaHandle"))
		];
}

FReply SDisplayClusterConfiguratorResizer::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bResizing = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SDisplayClusterConfiguratorResizer::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bResizing = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SDisplayClusterConfiguratorResizer::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bResizing)
	{
		TSharedPtr<SDisplayClusterConfiguratorBaseNode> BaseNode = BaseNodePtr.Pin();
		check(BaseNode.IsValid())

		TSharedPtr<SGraphPanel> GraphPanel = BaseNode->GetOwnerPanel();
		check(GraphPanel.IsValid())

		FVector2D NewNodeSize = MouseEvent.GetScreenSpacePosition() - BaseNode->GetTickSpaceGeometry().GetAbsolutePosition();

		NewNodeSize /= GraphPanel->GetZoomAmount();

		// Never node size less then 0
		if (NewNodeSize.X < 0.f)
		{
			NewNodeSize.X = 0.f;
		}
		if (NewNodeSize.Y < 0.f)
		{
			NewNodeSize.Y = 0.f;
		}

		BaseNode->SetNodeSize(NewNodeSize);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
