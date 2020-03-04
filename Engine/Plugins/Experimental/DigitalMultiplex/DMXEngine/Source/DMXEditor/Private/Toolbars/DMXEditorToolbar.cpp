// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolbars/DMXEditorToolbar.h"
#include "DMXEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "KismetToolbar"

void FDMXEditorToolbar::AddCompileToolbar(TSharedPtr<FExtender> Extender)
{
	TSharedPtr<FDMXEditor> DMXEditorPtr = DMXEditor.Pin();

	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		DMXEditorPtr->GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FDMXEditorToolbar::FillCompileToolbar));
}

FSlateIcon FDMXEditorToolbar::GetStatusImage() const
{
	return FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Status.Good");
}

FText FDMXEditorToolbar::GetStatusTooltip() const
{
	return LOCTEXT("Default_Status", "Good to go");
}

void FDMXEditorToolbar::FillCompileToolbar(FToolBarBuilder& ToolbarBuilder)
{
	// TODO. Implement custom toolbar
	ToolbarBuilder.BeginSection("CompileToolbar");
	ToolbarBuilder.EndSection();
}

#undef LOCTEXT_NAMESPACE