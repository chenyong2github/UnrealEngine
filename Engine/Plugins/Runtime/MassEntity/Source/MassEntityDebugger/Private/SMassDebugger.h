// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/Docking/SDockTab.h"


class SMassDebuggerTab : public SDockTab
{
public:
	virtual bool SupportsKeyboardFocus() const override { return true; }
};

class SMassDebugger : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMassDebugger) 
	{
	}
	SLATE_END_ARGS()

public:

	/**
	* Constructs the application.
	*
	* @param InArgs The Slate argument list.
	* @param ConstructUnderMajorTab The major tab which will contain the session front-end.
	* @param ConstructUnderWindow The window in which this widget is being constructed.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

	virtual bool SupportsKeyboardFocus() const override { return true; }
};
