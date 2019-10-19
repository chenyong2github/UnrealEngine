// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SAssetEditorViewport.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "EditorViewportCommands.h"
#include "EditorViewportTabContent.h"


void SAssetEditorViewport::BindCommands()
{
	FUICommandList& CommandListRef = *CommandList;

	const FEditorViewportCommands& Commands = FEditorViewportCommands::Get();

	TSharedRef<FEditorViewportClient> ClientRef = Client.ToSharedRef();

	CommandListRef.MapAction(
		Commands.ViewportConfig_OnePane,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::OnePane),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::OnePane));
	
	CommandListRef.MapAction(
		Commands.ViewportConfig_TwoPanesH,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::TwoPanesHoriz),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::TwoPanesHoriz));

	CommandListRef.MapAction(
		Commands.ViewportConfig_TwoPanesV,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::TwoPanesVert),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::TwoPanesVert));

	CommandListRef.MapAction(
		Commands.ViewportConfig_ThreePanesLeft,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::ThreePanesLeft),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::ThreePanesLeft));

	CommandListRef.MapAction(
		Commands.ViewportConfig_ThreePanesRight,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::ThreePanesRight),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::ThreePanesRight));

	CommandListRef.MapAction(
		Commands.ViewportConfig_ThreePanesTop,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::ThreePanesTop),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::ThreePanesTop));

	CommandListRef.MapAction(
		Commands.ViewportConfig_ThreePanesBottom,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::ThreePanesBottom),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::ThreePanesBottom));

	CommandListRef.MapAction(
		Commands.ViewportConfig_FourPanesLeft,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::FourPanesLeft),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::FourPanesLeft));

	CommandListRef.MapAction(
		Commands.ViewportConfig_FourPanesRight,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::FourPanesRight),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::FourPanesRight));

	CommandListRef.MapAction(
		Commands.ViewportConfig_FourPanesTop,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::FourPanesTop),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::FourPanesTop));

	CommandListRef.MapAction(
		Commands.ViewportConfig_FourPanesBottom,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::FourPanesBottom),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::FourPanesBottom));

	CommandListRef.MapAction(
		Commands.ViewportConfig_FourPanes2x2,
		FExecuteAction::CreateSP(this, &SAssetEditorViewport::OnSetViewportConfiguration, EditorViewportConfigurationNames::FourPanes2x2),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SAssetEditorViewport::IsViewportConfigurationSet, EditorViewportConfigurationNames::FourPanes2x2));

	SEditorViewport::BindCommands();
}


void SAssetEditorViewport::OnSetViewportConfiguration(FName ConfigurationName)
{
	TSharedPtr<FAssetEditorViewportLayout> LayoutPinned = ParentLayout.Pin();
	if (LayoutPinned.IsValid())
	{
		TSharedPtr<FViewportTabContent> ViewportTabPinned = LayoutPinned->GetParentTabContent().Pin();
		if (ViewportTabPinned.IsValid())
		{
			ViewportTabPinned->SetViewportConfiguration(ConfigurationName);
 			FSlateApplication::Get().DismissAllMenus();
		}
	}
}

bool SAssetEditorViewport::IsViewportConfigurationSet(FName ConfigurationName) const
{
	TSharedPtr<FAssetEditorViewportLayout> LayoutPinned = ParentLayout.Pin();
	if (LayoutPinned.IsValid())
	{
		TSharedPtr<FViewportTabContent> ViewportTabPinned = LayoutPinned->GetParentTabContent().Pin();
		if (ViewportTabPinned.IsValid())
		{
			return ViewportTabPinned->IsViewportConfigurationSet(ConfigurationName);
		}
	}
	return false;
}
