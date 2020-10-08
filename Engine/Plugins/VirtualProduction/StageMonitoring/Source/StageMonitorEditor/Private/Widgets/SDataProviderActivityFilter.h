// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"


#include "CoreMinimal.h"
#include "IStageMonitorSession.h"
#include "SlateFwd.h"
#include "StageMessages.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class FStructOnScope;
class FMenuBuilder;
class IStageMonitorSession;

/**
 *
 */
class FDataProviderActivityFilter
{
public:
	FDataProviderActivityFilter(TWeakPtr<IStageMonitorSession> InSession);

	/** Returns true if the entry passes the current filter */
	bool DoesItPass(TSharedPtr<FStageDataEntry>& Entry) const;

public:
	TArray<UScriptStruct*> RestrictedTypes;
	TArray<FStageSessionProviderEntry> RestrictedProviders;
	TArray<FName> RestrictedSources;
	bool bEnableCriticalStateFilter = false;
	TWeakPtr<IStageMonitorSession> Session;
};


/**
 *
 */
class SDataProviderActivityFilter : public SCompoundWidget
{
	using Super = SCompoundWidget;

public:
	SLATE_BEGIN_ARGS(SDataProviderActivityFilter) {}
		SLATE_EVENT(FSimpleDelegate, OnActivityFilterChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakPtr<IStageMonitorSession>& InSession);

	/** Returns the activity filter */
	const FDataProviderActivityFilter& GetActivityFilter() const { return *CurrentFilter; }

	/** Refreshes session used to fetch data and update UI */
	void RefreshMonitorSession(const TWeakPtr<IStageMonitorSession>& NewSession);

private:

	/** Toggles state of provider filter */
	void ToggleProviderFilter(FStageSessionProviderEntry Provider);

	/** Returns true if provider is currently filtered out */
	bool IsProviderFiltered(FStageSessionProviderEntry Provider) const;

	/** Toggles state of data type filter */
	void ToggleDataTypeFilter(UScriptStruct* Type);
	
	/** Returns true if data type is currently filtered out */
	bool IsDataTypeFiltered(UScriptStruct* Type) const;

	/** Toggles state of critical state source filter */
	void ToggleCriticalStateSourceFilter(FName Source);

	/** Returns true if critical state source is currently filtered out */
	bool IsCriticalStateSourceFiltered(FName Source) const;

	/** Toggles whether critical source filtering is enabled or not */
	void ToggleCriticalSourceEnabledFilter();

	/** Returns true if filtering by critical state source enabled */
	bool IsCriticalSourceFilteringEnabled() const;

	/** Create the AddFilter menu when combo button is clicked. Different filter types will be submenus */
	TSharedRef<SWidget> MakeAddFilterMenu();

	/** Creates the menu listing the different message types filter */
	void CreateMessageTypeFilterMenu(FMenuBuilder& MenuBuilder);

	/** Creates the menu listing the different providers filter */
	void CreateProviderFilterMenu(FMenuBuilder& MenuBuilder);

	/** Create the menu listing all critical state sources */
	void CreateCriticalStateSourceFilterMenu(FMenuBuilder& MenuBuilder);

	/** Binds this widget to some session callbacks */
	void AttachToMonitorSession(const TWeakPtr<IStageMonitorSession>& NewSession);

private:

	/** Current state of the provider data filter */
	TUniquePtr<FDataProviderActivityFilter> CurrentFilter;

	/** Delegate fired when filter changed */
	FSimpleDelegate OnActivityFilterChanged;

	/** Weakptr to the current session we're sourcing info from */
	TWeakPtr<IStageMonitorSession> Session;
};

