// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STreeView.h"

// Insights
#include "Insights/ViewModels/Filters.h"
#include "Insights/ViewModels/FilterConfiguratorNode.h"

class SDockTab;

namespace Insights
{

class SFilterConfigurator;
class FQuickFind;
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A custom widget used to configure custom filters.
 */
class SQuickFind: public SCompoundWidget
{
public:
	SQuickFind();

	virtual ~SQuickFind();

	SLATE_BEGIN_ARGS(SQuickFind) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FQuickFind> InFilterConfiguratorViewModel);

	void Reset();

	void SetParentTab(const TSharedPtr<SDockTab> InTab) { ParentTab = InTab; }
	const TWeakPtr<SDockTab> GetParentTab() { return ParentTab; };

	static void RegisterQuickFindTab();

	static TSharedPtr<SQuickFind> CreateAndOpenQuickFilterWidget(TSharedPtr<FQuickFind> InQuickFindViewModel);

private:
	void InitCommandList();

	FReply FindNext_OnClicked();

	FReply FindPrevious_OnClicked();

	FReply FilterAll_OnClicked();

	FReply ClearFilters_OnClicked();

	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

private:
	static const FName QuickFindTabId;

	static TSharedPtr<SQuickFind> PendingWidget;

	static bool bIsTabRegistered;

	TSharedPtr<SFilterConfigurator> FilterConfigurator;

	TSharedPtr<FQuickFind> QuickFindViewModel;

	TWeakPtr<SDockTab> ParentTab;

	FDelegateHandle OnViewModelDestroyedHandle;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
