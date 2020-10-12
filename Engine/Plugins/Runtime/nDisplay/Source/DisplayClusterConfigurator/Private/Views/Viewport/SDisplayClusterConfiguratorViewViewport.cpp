// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/SDisplayClusterConfiguratorViewViewport.h"
#include "Views/Viewport/SDisplayClusterConfiguratorViewport.h"

#include "DisplayClusterConfiguratorToolkit.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorViewViewport"


void SDisplayClusterConfiguratorViewViewport::Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene)
{
	ToolkitPtr = InToolkit;

	ViewportWidget = SNew(SDisplayClusterConfiguratorViewport, InToolkit, InPreviewScene);

	TSharedPtr<SVerticalBox> ViewportContainer = nullptr;
	SDisplayClusterConfiguratorViewBase::Construct(
		SDisplayClusterConfiguratorViewBase::FArguments()
		.Padding(0.0f)
		.Content()
		[
			SAssignNew(ViewportContainer, SVerticalBox)

			// Build our toolbar level toolbar
			+SVerticalBox::Slot()
			.FillHeight(1)
			[
				SNew(SOverlay)

				// The viewport
				+SOverlay::Slot()
				[
					ViewportWidget.ToSharedRef()
				]

				// The 'dirty/in-error' indicator text in the bottom-right corner
				+SOverlay::Slot()
				.Padding(8)
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Right)
				[
					SAssignNew(ViewportNotificationsContainer, SVerticalBox)
				]
			]
		],
		InToolkit);
}

void SDisplayClusterConfiguratorViewViewport::PostUndo(bool bSuccess)
{
}

void SDisplayClusterConfiguratorViewViewport::PostRedo(bool bSuccess)
{
}

#undef LOCTEXT_NAMESPACE
