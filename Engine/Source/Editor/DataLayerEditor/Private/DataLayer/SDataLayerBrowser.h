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

enum class EDataLayerBrowserMode
{
	DataLayers,
	DataLayerContents,
	Count
};

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

	EDataLayerBrowserMode GetMode() const { return Mode; }

	/** Broadcasts whenever one or more Actors changed DataLayers*/
	DECLARE_EVENT_OneParam(SDataLayerBrowser, FOnModeChanged, EDataLayerBrowserMode /*Mode*/);
	virtual FOnModeChanged& OnModeChanged() final { return ModeChanged; }

private:

	void SetupDataLayerMode(EDataLayerBrowserMode InNewMode);
	
	/**	Fires whenever one or more actor DataLayer changes */
	FOnModeChanged ModeChanged;

	TSharedPtr<SButton> ToggleModeButton;
	TSharedPtr<SVerticalBox> ContentAreaBox;
	TSharedPtr<SBorder> DataLayerContentsSection;
	TSharedPtr<SBorder> DataLayerContentsHeader;
	EDataLayerBrowserMode Mode;
};
