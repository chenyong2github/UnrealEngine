// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Widgets/Views/STreeView.h"

#include "SourceFilterService.h"
#include "ISessionSourceFilterService.h"
#include "IFilterObject.h"

#if WITH_EDITOR
#include "TraceSourceFilteringSettings.h"
#endif // WITH_EDITOR

class IDetailsView;
class SWorldTraceFilteringWidget;
class SComboButton;
class SHorizontalBox;

class STraceSourceFilteringWidget : public SCompoundWidget
{
	friend class SSourceFilteringTreeView;
public:
	/** Default constructor. */
	STraceSourceFilteringWidget();

	/** Virtual destructor. */
	virtual ~STraceSourceFilteringWidget();

	SLATE_BEGIN_ARGS(STraceSourceFilteringWidget) {}
	SLATE_END_ARGS()

	/** Constructs this widget. */
	void Construct(const FArguments& InArgs);

	/** Begin SCompoundWidget overrides */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	/** End SCompoundWidget overrides */
	
protected:
	void ConstructMenuBox();
	void ConstructTreeview();
#if WITH_EDITOR
	void ConstructInstanceDetailsView();
#endif // WITH_EDITOR

	TSharedRef<SWidget> OnGetOptionsMenu();
	TSharedPtr<SWidget> OnContextMenuOpening();

	/** Callback for whenever a different analysis session (store) has been retrieved */
	void SetCurrentAnalysisSession(uint32 SessionHandle, TSharedRef<const Trace::IAnalysisSession> AnalysisSession);

	/** Returns whether or not a valid ISessionSourceFilterService is available / set */
	bool HasValidFilterSession() const;

	/** Returns visibility state for SThrobber, used to indicate pending filter session request */
	EVisibility GetThrobberVisibility() const;

	/** Returns whether or not the contained widgets should be enabled, determined by having a valid session and no pending request */
	bool ShouldWidgetsBeEnabled() const;

	/** Refreshes the filtering data and state, using SessionFilterService, represented by this widget */
	void RefreshFilteringData();
	
	/** Save current UTraceSourceFilteringSettings state to INI files */
	void SaveFilteringSettings();

	/** Saves and restores the current selection and expansion state of FilterTreeView*/
	void SaveTreeviewState();
	void RestoreTreeviewState();
protected:
#if WITH_EDITOR	
	/** Details view, used for displaying selected Filter UProperties */
	TSharedPtr<IDetailsView> FilterInstanceDetailsView;	
#endif // WITH_EDITOR
	
	/** Slate widget used to add filter instances to the session */
	TSharedPtr<SComboButton> AddFilterButton;

	/** Slate widget containing the Add Filter and Options widgets, used for disabling/enabling according to the session state */
	TSharedPtr<SHorizontalBox> MenuBox;

	/** Filter session instance, used to retrieve data and communicate with connected application */
	TSharedPtr<ISessionSourceFilterService> SessionFilterService;

	/** Panel used for filtering UWorld's traceability on the connected filter session */
	TSharedPtr<SWorldTraceFilteringWidget> WorldFilterWidget;

	/** Treeview used to display all currently represented Filters */
	TSharedPtr<STreeView<TSharedPtr<IFilterObject>>> FilterTreeView;

	/** Data used to populate the Filter Treeview */
	TArray<TSharedPtr<IFilterObject>> FilterObjects;
	TMap<TSharedPtr<IFilterObject>, TArray<TSharedPtr<IFilterObject>>> ParentToChildren;
	TArray<TSharedPtr<IFilterObject>> FlatFilterObjects;
	   
	/** Timestamp at which the treeview data was last retrieved from SessionFilterService */
	FDateTime SyncTimestamp;

	/** Used to store hash values of, treeview expanded and selected, filter instances when refreshing the treeview data */
	TArray<int32> ExpandedFilters;	
	TArray<int32> SelectedFilters;

	/** Cached pointer to Filtering Settings retrieved from SessionFilterService */
	UTraceSourceFilteringSettings* FilteringSettings;
};
