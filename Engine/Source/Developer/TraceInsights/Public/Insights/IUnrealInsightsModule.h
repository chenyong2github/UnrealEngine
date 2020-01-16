// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/TabManager.h"

class FExtender;

/** Major tab IDs for Insights tools */
struct TRACEINSIGHTS_API FInsightsManagerTabs
{
	static const FName StartPageTabId;
	static const FName SessionInfoTabId;
	static const FName TimingProfilerTabId;
	static const FName LoadingProfilerTabId;
	static const FName NetworkingProfilerTabId;
};

/** Tab IDs for the timing profiler */
struct TRACEINSIGHTS_API FTimingProfilerTabs
{
	// Tab identifiers
	static const FName ToolbarID;
	static const FName FramesTrackID;
	static const FName TimingViewID;
	static const FName TimersID;
	static const FName CallersID;
	static const FName CalleesID;
	static const FName StatsCountersID;
	static const FName LogViewID;
};

/** Delegate invoked when a major tab is created */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInsightsMajorTabCreated, FName /*MajorTabId*/, TSharedRef<FTabManager> /*TabManager*/)

/** Configuration for an Insights minor tab. This is used to augment the standard supplied tabs from plugins. */
struct TRACEINSIGHTS_API FInsightsMinorTabConfig
{
	FName TabId;

	FText TabLabel;

	FText TabTooltip;

	FSlateIcon TabIcon;

	FOnSpawnTab OnSpawnTab;

	FOnFindTabToReuse OnFindTabToReuse;

	TSharedPtr<FWorkspaceItem> WorkspaceGroup;
};

/** Configuration for an Insights major tab */
struct TRACEINSIGHTS_API FInsightsMajorTabConfig
{
	FInsightsMajorTabConfig()
		: Layout(nullptr)
		, WorkspaceGroup(nullptr)
		, bIsAvailable(true)
	{}

	/** Helper function for creating unavailable tab configs */
	static FInsightsMajorTabConfig Unavailable()
	{
		FInsightsMajorTabConfig Config;
		Config.bIsAvailable = false;
		return Config;
	}

	/** Identifier for this config */
	FName ConfigId;

	/** Display name for this config */
	FText ConfigDisplayName;

	/** Label for the tab. If this is not set the default will be used */
	TOptional<FText> TabLabel;

	/** Tooltip for the tab. If this is not set the default will be used */
	TOptional<FText> TabTooltip;

	/** Icon for the tab. If this is not set the default will be used */
	TOptional<FSlateIcon> TabIcon;

	/** The tab layout to use. If not specified, the default will be used. */
	TSharedPtr<FTabManager::FLayout> Layout;

	/** The menu workspace group to use. If not specified, the default will be used. */
	TSharedPtr<FWorkspaceItem> WorkspaceGroup;

	/** Extender used to add to the menu for this tab */
	TSharedPtr<FExtender> MenuExtender;

	/** Any additional minor tabs to add */
	TArray<FInsightsMinorTabConfig> MinorTabs;

	/** Whether the tab is available for selection (i.e. registered with the tab manager) */
	bool bIsAvailable;
};

/** Interface for an Unreal Insights module. */
class IUnrealInsightsModule : public IModuleInterface
{
public:
	/**
	 * Called when the application starts in "Browser" mode.
	 */
	virtual void CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess) = 0;

	/**
	 * Called when the application starts in "Viewer" mode.
	 */
	virtual void CreateSessionViewer(bool bAllowDebugTools) = 0;

	/**
	 * Starts analysis of the specified *.utrace file. Called when the application starts in "Viewer" mode.
	 *
	 * @param InTraceFile The file path to the *.utrace file to analyze.
	 */
	virtual void StartAnalysisForTraceFile(const TCHAR* InTraceFile) = 0;

	/**
	 * Starts analysis of the specified session. Called when the application starts in "Viewer" mode.
	 *
	 * @param InSessionId The id of the session to analyze.
	 */
	virtual void StartAnalysisForSession(const TCHAR* InSessionId) = 0;

	/**
	 * Starts analysis of the last live session. Called when the application starts in "Viewer" mode.
	 */
	virtual void StartAnalysisForLastLiveSession() = 0;

	/**
	 * Called when the application shutsdown.
	 */
	virtual void ShutdownUserInterface() = 0;

	/** 
	 * Registers a major tab layout. This defines how the major tab will appear when spawned.
	 * If this is not called prior to tabs being spawned then the built-in default layout will be used.
	 * @param	InMajorTabId	The major tab ID we are supplying a layout to
	 * @param	InConfig		The config to use when spawning the major tab
	 */
	virtual void RegisterMajorTabConfig(const FName& InMajorTabId, const FInsightsMajorTabConfig& InConfig) = 0;

	/** 
	 * Unregisters a major tab layout. This will revert the major tab to spawning with its default layout
	 * @param	InMajorTabId	The major tab ID we are supplying a layout to
	 */
	virtual void UnregisterMajorTabConfig(const FName& InMajorTabId) = 0;

	/** Callback invoked when a major tab is created */
	virtual FOnInsightsMajorTabCreated& OnMajorTabCreated() = 0;
};
