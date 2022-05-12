// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SConcertTabViewBase.h"

/** Base class for tab views that create sub-tabs. */
class SConcertTabViewWithManagerBase : public SConcertTabViewBase
{
public:

	DECLARE_DELEGATE_TwoParams(FCreateTabs, const TSharedRef<FTabManager>& /*TabManager*/, const TSharedRef<FTabManager::FLayout>& /*Layout*/);
	
	SLATE_BEGIN_ARGS(SConcertTabViewWithManagerBase) {}
		/** Which major tab to construct the sub-tabs under. */
		SLATE_ARGUMENT(TSharedPtr<SDockTab>, ConstructUnderMajorTab)
		/** The window in which the sub-tabs will be created. */
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ConstructUnderWindow)
		/** Callback for creating the sub-tabs */
		SLATE_EVENT(FCreateTabs, CreateTabs)
		/** Name to give the layout. Important for saving config. */
		SLATE_ARGUMENT(FName, LayoutName)
	SLATE_END_ARGS()

	/**
	 * @param InArgs
	 * @param InStatusBarId Unique ID needed for the status bar
	 */
	void Construct(const FArguments& InArgs, FName InStatusBarId);

private:

	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;
	
	TSharedRef<SWidget> CreateTabs(const FArguments& InArgs);
};
