// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"

/**
 * Slate window used to show import/export options for the USDImporter plugin
 */
class USDSTAGEIMPORTER_API SUsdOptionsWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SUsdOptionsWindow ) : _OptionsObject( nullptr ) {}
		SLATE_ARGUMENT( UObject*, OptionsObject )
		SLATE_ARGUMENT( TSharedPtr<SWindow>, WidgetWindow )
		SLATE_ARGUMENT( FText, AcceptText )
	SLATE_END_ARGS()

public:
	static bool ShowImportExportOptions( UObject& OptionsObject, bool bIsImport );
	static bool ShowOptions( UObject& OptionsObject, const FText& WindowTitle, const FText& AcceptText );

	void Construct( const FArguments& InArgs );
	virtual bool SupportsKeyboardFocus() const override;

	FReply OnAccept();
	FReply OnCancel();

	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override;
	bool UserAccepted() const;

private:
	UObject* OptionsObject;
	TWeakPtr< SWindow > Window;
	FText AcceptText;
	bool bAccepted;
};
