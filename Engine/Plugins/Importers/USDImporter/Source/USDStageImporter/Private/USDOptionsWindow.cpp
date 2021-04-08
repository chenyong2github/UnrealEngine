// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDOptionsWindow.h"

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "USDOptionsWindow"

bool SUsdOptionsWindow::ShowOptions( UObject& OptionsObject, bool bIsImport )
{
	TSharedPtr<SWindow> ParentWindow;

	if ( FModuleManager::Get().IsModuleLoaded( "MainFrame" ) )
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
		ParentWindow = MainFrame.GetParentWindow();
	}

	TSharedRef<SWindow> Window = SNew( SWindow )
		.Title( bIsImport
			? LOCTEXT( "USDImportOptionsTitle", "USD Import Options" )
			: LOCTEXT( "USDExportOptionsTitle", "USD Export Options" )
		)
		.SizingRule( ESizingRule::Autosized );

	TSharedPtr<SUsdOptionsWindow> OptionsWindow;
	Window->SetContent
	(
		SAssignNew( OptionsWindow, SUsdOptionsWindow )
		.OptionsObject( &OptionsObject )
		.IsImport( bIsImport )
		.WidgetWindow( Window )
	);

	const bool bSlowTaskWindow = false;
	FSlateApplication::Get().AddModalWindow( Window, ParentWindow, bSlowTaskWindow );

	return OptionsWindow->UserAccepted();
}

void SUsdOptionsWindow::Construct( const FArguments& InArgs )
{
	OptionsObject = InArgs._OptionsObject;
	Window = InArgs._WidgetWindow;
	bIsImport = InArgs._IsImport;
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
				.Text( bIsImport
					? LOCTEXT( "USDOptionWindow_Import", "Import" )
					: LOCTEXT( "USDOptionWindow_Export", "Export" )
				)
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