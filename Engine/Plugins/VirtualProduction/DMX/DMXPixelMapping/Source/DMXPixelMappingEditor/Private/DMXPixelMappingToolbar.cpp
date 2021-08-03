// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMappingToolbar.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "DMXPixelMappingEditorCommands.h"
#include "DMXPixelMappingEditorCommon.h"
#include "DMXPixelMappingEditorStyle.h"
#include "DMXPixelMappingEditorCommon.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FDMXPixelMappingToolbar"

FDMXPixelMappingToolbar::FDMXPixelMappingToolbar(TSharedPtr<FDMXPixelMappingToolkit> InToolkit)
	: ToolkitWeakPtr(InToolkit)
{}

void FDMXPixelMappingToolbar::BuildToolbar(TSharedPtr<FExtender> Extender)
{
	TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin();
	check(Toolkit.IsValid());

	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		Toolkit->GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FDMXPixelMappingToolbar::Build)
	);
}

void FDMXPixelMappingToolbar::Build(FToolBarBuilder& ToolbarBuilder)
{
	AddHelpersSection(ToolbarBuilder);
	AddPlayAndStopSection(ToolbarBuilder);
}

void FDMXPixelMappingToolbar::AddHelpersSection(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Thumbnail");
	{
		ToolbarBuilder.AddToolBarButton(FDMXPixelMappingEditorCommands::Get().SaveThumbnailImage, NAME_None,
			LOCTEXT("GenerateThumbnail", "Thumbnail"),
			LOCTEXT("GenerateThumbnailTooltip", "Generate a thumbnail image."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Cascade.SaveThumbnailImage"));
	}
	ToolbarBuilder.EndSection();
}

void FDMXPixelMappingToolbar::AddPlayAndStopSection(FToolBarBuilder& ToolbarBuilder)
{
	TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin();
	check(Toolkit.IsValid());

	ToolbarBuilder.BeginSection("Renderers");
	{
		ToolbarBuilder.AddToolBarButton(FDMXPixelMappingEditorCommands::Get().AddMapping,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(),
			FSlateIcon(FDMXPixelMappingEditorStyle::GetStyleSetName(), "DMXPixelMappingEditor.AddMapping"),
			FName(TEXT("Add Mapping")));
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("PlayAndStopDMX");
	{
		ToolbarBuilder.AddToolBarButton(FDMXPixelMappingEditorCommands::Get().PlayDMX,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(),
			FSlateIcon(FDMXPixelMappingEditorStyle::GetStyleSetName(), "DMXPixelMappingEditor.PlayDMX"),
			FName(TEXT("Play DMX")));

		ToolbarBuilder.AddToolBarButton(FDMXPixelMappingEditorCommands::Get().StopPlayingDMX,
			NAME_None, TAttribute<FText>(), TAttribute<FText>(),
			FSlateIcon(FDMXPixelMappingEditorStyle::GetStyleSetName(), "DMXPixelMappingEditor.StopPlayingDMX"),
			FName(TEXT("Stop Playing DMX")));

		FUIAction PlayDMXOptionsAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateLambda([this] { return !ToolkitWeakPtr.Pin()->IsPlayingDMX(); })
			);

		ToolbarBuilder.AddComboButton(
			PlayDMXOptionsAction,
			FOnGetContent::CreateSP(this, &FDMXPixelMappingToolbar::FillPlayMenu),
			LOCTEXT("PlayDMXOptions", "Play DMX Options"),
			LOCTEXT("PlayDMXOptions", "Play DMX Options"),
			FSlateIcon(),
			true);
	}
	ToolbarBuilder.EndSection();
}

TSharedRef<SWidget> FDMXPixelMappingToolbar::FillPlayMenu()
{
	TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin();
	check(Toolkit.IsValid());

	FMenuBuilder MenuBuilder(true, Toolkit->GetToolkitCommands());

	if (!Toolkit->IsPlayingDMX())
	{
		MenuBuilder.BeginSection("bTogglePlayDMXAll");
		{
			MenuBuilder.AddMenuEntry(FDMXPixelMappingEditorCommands::Get().bTogglePlayDMXAll);
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE