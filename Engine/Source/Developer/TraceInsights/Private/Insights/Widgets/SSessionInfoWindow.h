// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Misc/Guid.h"
#include "SlateFwd.h"
#include "TraceServices/ModuleService.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Views/SListView.h"

// Insights
#include "Insights/InsightsManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FActiveTimerHandle;
class SVerticalBox;
class SEditableTextBox;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FSessionInfoTabs
{
	// Tab identifiers
	static const FName SessionInfoID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Implements the Start Page window. */
class SSessionInfoWindow : public SCompoundWidget
{
public:
	/** Default constructor. */
	SSessionInfoWindow();

	/** Virtual destructor. */
	virtual ~SSessionInfoWindow();

	SLATE_BEGIN_ARGS(SSessionInfoWindow) {}
	SLATE_END_ARGS()

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

private:
	/** Updates the amount of time the profiler has been active. */
	EActiveTimerReturnType UpdateActiveDuration(double InCurrentTime, float InDeltaTime);

	/**
	 * Ticks this widget.  Override in derived classes, but always call the parent implementation.
	 *
	 * @param  AllottedGeometry The space allotted for this widget
	 * @param  InCurrentTime  Current absolute real time
	 * @param  InDeltaTime  Real time passed since last tick
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/**
	 * The system will use this event to notify a widget that the cursor has entered it. This event is NOT bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 */
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/**
	 * The system will use this event to notify a widget that the cursor has left it. This event is NOT bubbled.
	 *
	 * @param MouseEvent Information about the input event
	 */
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	/**
	 * Called after a key is pressed when this widget has focus
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param  InKeyEvent  Key event
	 *
	 * @return  Returns whether the event was handled, along with other possible actions
	 */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/**
	 * Called when the user is dropping something onto a widget; terminates drag and drop.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether this event was handled.
	 */
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	/**
	 * Called during drag and drop when the the mouse is being dragged over a widget.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether this event was handled.
	 */
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)  override;

	/**
	 * Fill the main menu with menu items.
	 *
	 * @param MenuBuilder The multi-box builder that should be filled with content for this pull-down menu.
	 * @param TabManager A Tab Manager from which to populate tab spawner menu items.
	 */
	static void FillMenu(FMenuBuilder& MenuBuilder, const TSharedPtr<FTabManager> TabManager);


private:
	void AddInfoLine(TSharedPtr<SVerticalBox> InVerticalBox, const FText& InHeader, const TAttribute<FText>& InValue) const;

	TSharedRef<SDockTab> SpawnTab_SessionInfo(const FSpawnTabArgs& Args);
	void OnSessionInfoTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	FText GetSessionNameText() const;
	FText GetUriText() const;
	FText GetPlatformText() const;
	FText GetAppNameText() const;
	FText GetBuildConfigText() const;
	FText GetBuildTargetText() const;
	FText GetCommandLineText() const;
	FText GetFileSizeText() const;
	FText GetStatusText() const;
	FText GetModulesText() const;

public:
	/** The number of seconds the profiler has been active */
	float DurationActive;

private:
	/** The handle to the active update duration tick */
	TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

	/** Holds the tab manager that manages the front-end's tabs. */
	TSharedPtr<FTabManager> TabManager;

	FText PlatformText;
	FText AppNameText;
	FText CommandLineText;
	FText BuildConfigurationTypeText;
	FText BuildTargetTypeText;
	bool bIsSessionInfoSet = false;

	//FText Uri;
	//FDateTime Timestamp;
	//uint64 Size;
	//bool bIsLive;
};
