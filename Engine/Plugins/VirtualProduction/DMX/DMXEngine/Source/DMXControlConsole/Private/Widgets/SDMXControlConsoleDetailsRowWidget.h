// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntityReference.h"

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;
class SBorder;


/** A Details Row to show/select a Fixture Patch from a DMX Library */
class SDMXControlConsoleDetailsRowWidget
	: public SCompoundWidget
{
	DECLARE_DELEGATE_OneParam(FDMXFixturePatchDetailsRowDelegate, const TSharedRef<SDMXControlConsoleDetailsRowWidget>&)

public:
	SLATE_BEGIN_ARGS(SDMXControlConsoleDetailsRowWidget)
	{}
		/** Delegate excecuted when this widget's button is clicked */
		SLATE_EVENT(FDMXFixturePatchDetailsRowDelegate, OnGenerateFromFixturePatch)

		/** Delegate excecuted when this widget is selected */
		SLATE_EVENT(FDMXFixturePatchDetailsRowDelegate, OnSelectFixturePatchDetailsRow)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, const FDMXEntityFixturePatchRef InFixturePatchRef);

	/** Selects this Details Row */
	void Select();

	/** Unselects this Details Row */
	void Unselect();

	/** True if this Fader Group is selected, otherwise false */
	bool IsSelected() const { return bSelected; }

	/** Gets Fixture Patch reference */
	const FDMXEntityFixturePatchRef& GetFixturePatchRef() const { return FixturePatchRef; }

protected:
	//~ Begin SWidget interface
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ End of SWidget interface

private:
	/** Called when this widget's button is clicked */
	FReply OnGenerateClicked();

	/** Gets border brush depending on selection state */
	const FSlateBrush* GetBorderImage() const;
	
	/** Gets button visibility depending on selection state */
	EVisibility GetGenerateButtonVisibility() const;

	/** The delegate to to excecute when this widget's button is clicked */
	FDMXFixturePatchDetailsRowDelegate OnGenerateFromFixturePatch;

	/** The delegate to to excecute when this widget is selected */
	FDMXFixturePatchDetailsRowDelegate OnSelectFixturePatchDetailsRow;

	/** Weak Object reference to the Fixture Patch this widget is based on */
	FDMXEntityFixturePatchRef FixturePatchRef;

	/** Shows wheter this Fader Group needs to be refreshed or not */
	bool bSelected = false;
};
