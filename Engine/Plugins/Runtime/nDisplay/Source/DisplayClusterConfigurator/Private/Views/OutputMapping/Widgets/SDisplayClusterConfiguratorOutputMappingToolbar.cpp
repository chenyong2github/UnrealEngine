// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorOutputMappingToolbar.h"

#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorStyle.h"
#include "Interfaces/IDisplayClusterConfigurator.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorOutputMappingToolbar"

void SDisplayClusterConfiguratorOutputMappingToolbar::Construct(const FArguments& InArgs)
{
	CommandList = InArgs._CommandList;

	ChildSlot
		[
			MakeToolBar(InArgs._Extenders)
		];

	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

TSharedRef<SWidget> SDisplayClusterConfiguratorOutputMappingToolbar::MakeToolBar(const TSharedPtr<FExtender> InExtenders)
{
	FToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);
	const FDisplayClusterConfiguratorCommands& Commands = IDisplayClusterConfigurator::Get().GetCommands();

	// Use a custom style
	FName ToolBarStyle = "ViewportMenu";
	ToolbarBuilder.SetStyle(&FEditorStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	ToolbarBuilder.BeginSection("View");
	{
		ToolbarBuilder.AddToolBarButton(Commands.ToggleWindowInfo,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ToggleWindowInfo"),
			"ToggleWindowInfo"
			);

		ToolbarBuilder.AddToolBarButton(Commands.ToggleWindowCornerImage,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ToggleWindowCornerImage"),
			"ToggleWindowCornerImage"
			);

		ToolbarBuilder.AddToolBarButton(Commands.ToggleOutsideViewports,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ToggleOutsideViewports"),
			"ToggleOutsideViewports"
			);

		ToolbarBuilder.AddToolBarButton(Commands.ZoomToFit,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ZoomToFit"),
			"ZoomToFit"
			);
	}
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
