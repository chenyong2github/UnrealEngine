// Copyright Epic Games, Inc. All Rights Reserved.

#include "NewAssetOrClassContextMenu.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "Settings/ContentBrowserSettings.h"
#include "ContentBrowserUtils.h"
#include "Widgets/SToolTip.h"
#include "ToolMenus.h"
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSubsystem.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

void FNewAssetOrClassContextMenu::MakeContextMenu(
	UToolMenu* Menu, 
	const TArray<FName>& InSelectedPaths,
	const FOnNewFolderRequested& InOnNewFolderRequested, 
	const FOnGetContentRequested& InOnGetContentRequested
	)
{
	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

	const FName FirstSelectedPath = (InSelectedPaths.Num() > 0) ? InSelectedPaths[0] : FName();
	const bool bIsValidNewFolderPath = ContentBrowserData->CanCreateFolder(FirstSelectedPath, nullptr);
	const bool bHasSinglePathSelected = InSelectedPaths.Num() == 1;

	auto CanExecuteFolderActions = [bHasSinglePathSelected, bIsValidNewFolderPath]() -> bool
	{
		// We can execute folder actions when we only have a single path selected, and that path is a valid path for creating a folder
		return bHasSinglePathSelected && bIsValidNewFolderPath;
	};
	const FCanExecuteAction CanExecuteFolderActionsDelegate = FCanExecuteAction::CreateLambda(CanExecuteFolderActions);

	// Get Content
	if ( InOnGetContentRequested.IsBound() )
	{
		{
			FToolMenuSection& Section = Menu->AddSection( "ContentBrowserGetContent", LOCTEXT( "GetContentMenuHeading", "Content" ) );
			Section.AddMenuEntry(
				"GetContent",
				LOCTEXT( "GetContentText", "Add Feature or Content Pack..." ),
				LOCTEXT( "GetContentTooltip", "Add features and content packs to the project." ),
				FSlateIcon( FEditorStyle::GetStyleSetName(), "ContentBrowser.AddContent" ),
				FUIAction( FExecuteAction::CreateStatic( &FNewAssetOrClassContextMenu::ExecuteGetContent, InOnGetContentRequested ) )
				);
		}
	}

	// New Folder
	if(InOnNewFolderRequested.IsBound() && GetDefault<UContentBrowserSettings>()->DisplayFolders)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("ContentBrowserNewFolder", LOCTEXT("FolderMenuHeading", "Folder") );
			FText NewFolderToolTip;
			if(bHasSinglePathSelected)
			{
				if(bIsValidNewFolderPath)
				{
					NewFolderToolTip = FText::Format(LOCTEXT("NewFolderTooltip_CreateIn", "Create a new folder in {0}."), FText::FromName(FirstSelectedPath));
				}
				else
				{
					NewFolderToolTip = FText::Format(LOCTEXT("NewFolderTooltip_InvalidPath", "Cannot create new folders in {0}."), FText::FromName(FirstSelectedPath));
				}
			}
			else
			{
				NewFolderToolTip = LOCTEXT("NewFolderTooltip_InvalidNumberOfPaths", "Can only create folders when there is a single path selected.");
			}

			Section.AddMenuEntry(
				"NewFolder",
				LOCTEXT("NewFolderLabel", "New Folder"),
				NewFolderToolTip,
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.NewFolderIcon"),
				FUIAction(
					FExecuteAction::CreateStatic(&FNewAssetOrClassContextMenu::ExecuteNewFolder, FirstSelectedPath, InOnNewFolderRequested),
					CanExecuteFolderActionsDelegate
					)
				);
		}
	}
}

void FNewAssetOrClassContextMenu::ExecuteNewFolder(FName InPath, FOnNewFolderRequested InOnNewFolderRequested)
{
	if (ensure(!InPath.IsNone()))
	{
		InOnNewFolderRequested.ExecuteIfBound(InPath.ToString());
	}
}

void FNewAssetOrClassContextMenu::ExecuteGetContent( FOnGetContentRequested InOnGetContentRequested )
{
	InOnGetContentRequested.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
