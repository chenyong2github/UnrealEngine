// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class FToolBarBuilder;

/** Ribbon based toolbar used as a main menu in the Profiler window. */
class STimingProfilerToolbar : public SCompoundWidget
{
public:
	/** Default constructor. */
	STimingProfilerToolbar();

	/** Virtual destructor. */
	virtual ~STimingProfilerToolbar();

	SLATE_BEGIN_ARGS(STimingProfilerToolbar) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct( const FArguments& InArgs );

private:
	/** Create the UI commands for the toolbar */
	void CreateCommands();

	bool ToggleModule_CanExecute(FName ModuleName) const;
	void ToggleModule_Execute(FName ModuleName);
	bool ToggleModule_IsChecked(FName ModuleName) const;
	ECheckBoxState ToggleModule_IsChecked2(FName ModuleName) const;
	void ToggleModule_OnCheckStateChanged(ECheckBoxState NewRadioState, FName ModuleName);
	void FillModulesToolbar(FToolBarBuilder& ToolbarBuilder);
};
