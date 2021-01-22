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
	CurrentAspectRatio = 1;
	bResizing = false;

	IsFixedAspectRatio = InArgs._IsFixedAspectRatio;

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

		// Store the current aspect ratio here so that it isn't suspectible to drift due to float->int conversion while dragging.
		TSharedPtr<SDisplayClusterConfiguratorBaseNode> BaseNode = BaseNodePtr.Pin();
		check(BaseNode.IsValid());

		const FVector2D CurrentNodeSize = BaseNode->GetSize();
		if (CurrentNodeSize.Y != 0)
		{
			CurrentAspectRatio = CurrentNodeSize.X / CurrentNodeSize.Y;
		}
		else
		{
			CurrentAspectRatio = 1;
		}

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

		bool bIsFixedAspectRatio = IsFixedAspectRatio.Get(false);
		if (bIsFixedAspectRatio)
		{
			// If the aspect ratio is fixed, first get the node's current size to compute the ratio from,
			// then force the new node size to match that aspect ratio.
			const FVector2D CurrentNodeSize = BaseNode->GetSize();

			if (NewNodeSize.X > NewNodeSize.Y * CurrentAspectRatio)
			{
				NewNodeSize.X = NewNodeSize.Y * CurrentAspectRatio;
			}
			else
			{
				NewNodeSize.Y = NewNodeSize.X / CurrentAspectRatio;
			}
		}

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

		BaseNode->SetNodeSize(NewNodeSize, bIsFixedAspectRatio);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
