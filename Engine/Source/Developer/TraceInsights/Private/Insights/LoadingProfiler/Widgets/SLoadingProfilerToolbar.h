// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class FToolBarBuilder;

/** Ribbon based toolbar used as a main menu in the Profiler window. */
class SLoadingProfilerToolbar : public SCompoundWidget
{
public:
	/** Default constructor. */
	SLoadingProfilerToolbar();

	/** Virtual destructor. */
	virtual ~SLoadingProfilerToolbar();

	SLATE_BEGIN_ARGS(SLoadingProfilerToolbar) {}
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
};
