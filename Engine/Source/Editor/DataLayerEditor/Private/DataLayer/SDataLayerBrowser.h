// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Styling/SlateColor.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ISceneOutliner;
class SButton;
class SVerticalBox;
class SBorder;
class UDataLayer;

//////////////////////////////////////////////////////////////////////////
// SDataLayerBrowser

class SDataLayerBrowser : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS( SDataLayerBrowser ) {}
	SLATE_END_ARGS()

	~SDataLayerBrowser(){}

	/**
	 * Construct this widget.  Called by the SNew() Slate macro.
	 *
	 * @param	InArgs				Declaration used by the SNew() macro to construct this widget
	 * @param	InViewModel			The UI logic not specific to slate
	 */
	void Construct(const FArguments& InArgs);

	void SyncDataLayerBrowserToDataLayer(const UDataLayer* DataLayer);
	void OnSelectionChanged(TSet<TWeakObjectPtr<const UDataLayer>>& SelectedDataLayersSet);

private:

	void InitializeDataLayerBrowser();

	TSet<TWeakObjectPtr<const UDataLayer>> SelectedDataLayersSet;
	TSharedPtr<class SDataLayerOutliner> DataLayerOutliner;
	TSharedPtr<class IDetailsView> DetailsWidget;
	TSharedPtr<SButton> ToggleModeButton;
	TSharedPtr<SVerticalBox> ContentAreaBox;
	TSharedPtr<SBorder> DataLayerContentsSection;
	TSharedPtr<SBorder> DataLayerContentsHeader;
};
