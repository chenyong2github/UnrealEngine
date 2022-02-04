// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "BindableProperty.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "IRewindDebuggerView.h"
#include "RewindDebuggerModule.h"
#include "SRewindDebuggerComponentTree.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SCompoundWidget.h"

class SDockTab;

class SRewindDebugger : public SCompoundWidget
{
	typedef TBindablePropertyInitializer<FString, BindingType_Out> DebugTargetInitializer;

public:
	DECLARE_DELEGATE_TwoParams( FOnScrubPositionChanged, double, bool )
	DECLARE_DELEGATE_OneParam( FOnDebugTargetChanged, TSharedPtr<FString> )
	DECLARE_DELEGATE_OneParam( FOnComponentDoubleClicked, TSharedPtr<FDebugObjectInfo> )
	DECLARE_DELEGATE_OneParam( FOnComponentSelectionChanged, TSharedPtr<FDebugObjectInfo> )
	DECLARE_DELEGATE_RetVal( TSharedPtr<SWidget>, FBuildComponentContextMenu )

	SLATE_BEGIN_ARGS(SRewindDebugger) { }
		SLATE_ARGUMENT( TArray< TSharedPtr< FDebugObjectInfo > >*, DebugComponents );
		SLATE_ARGUMENT(DebugTargetInitializer, DebugTargetActor);
		SLATE_ARGUMENT(TBindablePropertyInitializer<double>, TraceTime);
		SLATE_ARGUMENT(TBindablePropertyInitializer<float>, RecordingDuration);
		SLATE_ATTRIBUTE(double, ScrubTime);
		SLATE_EVENT( FOnScrubPositionChanged, OnScrubPositionChanged);
		SLATE_EVENT( FBuildComponentContextMenu, BuildComponentContextMenu );
		SLATE_EVENT( FOnComponentDoubleClicked, OnComponentDoubleClicked );
		SLATE_EVENT( FOnComponentSelectionChanged, OnComponentSelectionChanged );
	SLATE_END_ARGS()
	
public:

	/**
	* Default constructor.
	*/
	SRewindDebugger();
	virtual ~SRewindDebugger();

	/**
	* Constructs the application.
	*
	* @param InArgs The Slate argument list.
	* @param ConstructUnderMajorTab The major tab which will contain the session front-end.
	* @param ConstructUnderWindow The window in which this widget is being constructed.
	* @param InStyleSet The style set to use.
	*/
	void Construct(const FArguments& InArgs, TSharedRef<FUICommandList> CommandList, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

	void TrackCursor(bool bReverse);
	void RefreshDebugComponents();
	void CloseAllTabs();
private:
	void MainFrameCreationFinished(TSharedPtr<SWindow> InRootWindow, bool bIsNewProjectWindow);
	
	// Time Slider
	TAttribute<double> ScrubTimeAttribute;
	TAttribute<bool> TrackScrubbingAttribute;
	FOnScrubPositionChanged OnScrubPositionChanged;
	TRange<double> ViewRange;
	TBindableProperty<double> TraceTime;
	TBindableProperty<float> RecordingDuration;

	// debug actor selector
	TSharedRef<SWidget> MakeSelectActorMenu();
	void SetDebugTargetActor(AActor* Actor);
	FReply OnSelectActorClicked();

	TBindableProperty<FString, BindingType_Out> DebugTargetActor;
	TBindableProperty<float> DebugTargetAnimInstanceId;

	void TraceTimeChanged(double Time);

	TSharedRef<SWidget> MakeMainMenu();
	void MakeViewsMenu(FMenuBuilder& MenuBuilder);
	
	// component tree view
	TArray<TSharedPtr<FDebugObjectInfo>>* DebugComponents;
	TSharedPtr<FDebugObjectInfo> SelectedComponent;
    void ComponentSelectionChanged(TSharedPtr<FDebugObjectInfo> SelectedItem, ESelectInfo::Type SelectInfo);
	FBuildComponentContextMenu BuildComponentContextMenu;
	FOnComponentSelectionChanged OnComponentSelectionChanged;
	
	TSharedPtr<SRewindDebuggerComponentTree> ComponentTreeView;
	TSharedPtr<SWidget> OnContextMenuOpening();

	// Debug View Tabs
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args, FName ViewName);
	bool CanSpawnTab(const FSpawnTabArgs& Args, FName ViewName);
	void CloseTab(FName TabName);
	void OnPinnedTabClosed(TSharedRef<SDockTab> Tab);
	void ExtendTabMenu(FMenuBuilder& MenuBuilder, TSharedPtr<IRewindDebuggerView> View);
	void PinTab(TSharedPtr<IRewindDebuggerView> View);
	void ShowAllViews();
	void CreateDebugViews();
	void CreateDebugTabs();
	TArray<TSharedPtr<IRewindDebuggerView>> DebugViews;
	TArray<TSharedPtr<IRewindDebuggerView>> PinnedDebugViews;
	TArray<FName> TabNames;
	TArray<FName> HiddenTabs;  // keep track of tabs that have been closed so we don't automatically reopen them when switching components
	bool bInternalClosingTab = false;
	bool bInitializing = false;

	TSharedPtr<FTabManager> TabManager;
};
