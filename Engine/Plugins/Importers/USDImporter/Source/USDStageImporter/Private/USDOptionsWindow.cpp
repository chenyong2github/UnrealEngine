// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDOptionsWindow.h"

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "USDOptionsWindow"

bool SUsdOptionsWindow::ShowImportExportOptions( UObject& OptionsObject, bool bIsImport )
{
	if ( bIsImport )
	{
		return SUsdOptionsWindow::ShowOptions(
			OptionsObject,
			LOCTEXT( "USDImportOptionsTitle", "USD Import Options" ),
			LOCTEXT( "USDOptionWindow_Import", "Import" )
		);
	}
	else
	{
		return SUsdOptionsWindow::ShowOptions(
			OptionsObject,
			LOCTEXT( "USDExportOptionsTitle", "USD Export Options" ),
			LOCTEXT( "USDOptionWindow_Export", "Export" )
		);
	}
}

bool SUsdOptionsWindow::ShowOptions( UObject& OptionsObject, const FText& WindowTitle, const FText& AcceptText )
{
	TSharedPtr<SWindow> ParentWindow;

	if ( FModuleManager::Get().IsModuleLoaded( "MainFrame" ) )
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
		ParentWindow = MainFrame.GetParentWindow();
	}

	TSharedRef<SWindow> Window = SNew( SWindow )
		.Title( WindowTitle )
		.SizingRule( ESizingRule::Autosized );

	TSharedPtr<SUsdOptionsWindow> OptionsWindow;
	Window->SetContent
	(
		SAssignNew( OptionsWindow, SUsdOptionsWindow )
		.OptionsObject( &OptionsObject )
		.AcceptText( AcceptText )
		.WidgetWindow( Window )
	);

	// Preemptively make sure we have a progress dialog created before showing our modal. This because the progress
	// dialog itself is also modal. If it doesn't exist yet, and our options dialog causes a progress dialog
	// to be spawned (e.g. when switching the Level to export via the LevelSequenceUSDExporter), the progress dialog
	// will be pushed to the end of FSlateApplication::ActiveModalWindows (SlateApplication.cpp) and cause our options
	// dialog to pop out of its modal loop (FSlateApplication::AddModalWindow), instantly returning false to our caller
	FScopedSlowTask Progress( 1, LOCTEXT( "ShowingDialog", "Picking options..." ) );
	Progress.MakeDialog();

	const bool bSlowTaskWindow = false;
	FSlateApplication::Get().AddModalWindow( Window, ParentWindow, bSlowTaskWindow );

	return OptionsWindow->UserAccepted();
}

void SUsdOptionsWindow::Construct( const FArguments& InArgs )
{
	OptionsObject = InArgs._OptionsObject;
	Window = InArgs._WidgetWindow;
	AcceptText = InArgs._AcceptText;
	bAccepted = false;

	TSharedPtr<SBox> DetailsViewBox;

	ChildSlot
	[
		SNew( SVerticalBox )
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding( 2 )
		[
			SAssignNew( DetailsViewBox, SBox )
			.MaxDesiredHeight( 450.0f )
			.MinDesiredWidth( 550.0f )
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign( HAlign_Right )
		.Padding( 2 )
		[
			SNew( SUniformGridPanel )
			.SlotPadding( 2 )

			+ SUniformGridPanel::Slot( 0, 0 )
			[
				SNew( SButton )
				.HAlign( HAlign_Center )
				.Text( AcceptText )
				.OnClicked( this, &SUsdOptionsWindow::OnAccept )
			]

			+ SUniformGridPanel::Slot( 1, 0 )
			[
				SNew( SButton )
				.HAlign( HAlign_Center )
				.Text( LOCTEXT( "USDOptionWindow_Cancel", "Cancel" ) )
				.OnClicked( this, &SUsdOptionsWindow::OnCancel )
			]
		]
	];

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>( "PropertyEditor" );
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView( DetailsViewArgs );

	DetailsViewBox->SetContent( DetailsView.ToSharedRef() );
	DetailsView->SetObject( OptionsObject );
}

bool SUsdOptionsWindow::SupportsKeyboardFocus() const
{
	return true;
}

FReply SUsdOptionsWindow::OnAccept()
{
	if ( OptionsObject )
	{
		OptionsObject->SaveConfig();
	}

	bAccepted = true;
	if ( Window.IsValid() )
	{
		Window.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SUsdOptionsWindow::OnCancel()
{
	bAccepted = false;
	if ( Window.IsValid() )
	{
		Window.Pin()->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SUsdOptionsWindow::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	if ( InKeyEvent.GetKey() == EKeys::Escape )
	{
		return OnCancel();
	}
	return FReply::Unhandled();
}

bool SUsdOptionsWindow::UserAccepted() const
{
	return bAccepted;
}

#undef LOCTEXT_NAMESPACE