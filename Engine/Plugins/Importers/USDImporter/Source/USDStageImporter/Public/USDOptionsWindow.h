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
		SLATE_ARGUMENT( bool, IsImport )
	SLATE_END_ARGS()

public:
	static bool ShowOptions( UObject& OptionsObject, bool bIsImport = true );

	void Construct( const FArguments& InArgs );
	virtual bool SupportsKeyboardFocus() const override;

	FReply OnAccept();
	FReply OnCancel();

	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override;
	bool UserAccepted() const;

private:
	UObject* OptionsObject;
	TWeakPtr< SWindow > Window;
	bool bIsImport;
	bool bAccepted;
};
