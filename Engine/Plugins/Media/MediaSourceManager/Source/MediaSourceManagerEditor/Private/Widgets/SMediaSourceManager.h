// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SComboButton;
class SDockTab;
class SMediaSourceManagerSources;
class STextBlock;
class SVerticalBox;
class UMediaSourceManager;

/**
 * Implements the MediaSourceManager widget.
 */
class SMediaSourceManager
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManager) { }
	SLATE_END_ARGS()

	/**
	 * Construct this widget.
	 *
	 * @param InArgs					The declaration data for this widget.
	 */
	void Construct(const FArguments& InArgs);

private:

	/** Pointer to the object that is being viewed. */
	TWeakObjectPtr<UMediaSourceManager> MediaSourceManagerPtr;
	/** Widget that shows our manager. */
	TSharedPtr<SComboButton> ManagerButton;
	/** Widget that shows our sources. */
	TSharedPtr<SMediaSourceManagerSources> SourcesWidget;

	/**
	 * Gets the name of our manager.
	 */
	FText GetManagerName() const;

	/**
	 * Get a widget to pick the manager asset.
	 */
	TSharedRef<SWidget> GetManagerPicker();

	/**
	 * Called when a new manager is picked.
	 */
	void NewManagerSelected(const FAssetData& AssetData);

};
