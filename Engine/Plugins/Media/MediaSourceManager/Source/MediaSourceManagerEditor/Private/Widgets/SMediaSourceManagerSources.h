// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UMediaSourceManager;

/**
 * Implements the sources panel of the MediaSourceManager asset editor.
 */
class SMediaSourceManagerSources
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManagerSources) { }
	SLATE_END_ARGS()

public:

	/**
	 * Construct this widget.
	 *
	 * @param InArgs					The declaration data for this widget.
	 * @param InMediaSourceManager		The manager to show details for.
	 */
	void Construct(const FArguments& InArgs, UMediaSourceManager& InMediaSourceManager);

private:

	/** Pointer to the object that is being viewed. */
	TWeakObjectPtr<UMediaSourceManager> MediaSourceManager;
};
