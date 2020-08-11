// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"


#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "StageMessages.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class IStageMonitor;
class SStageMonitorPanel;
struct FDataProviderActivity;
class IStageDataCollection;
class IStructureDetailsView;
class FStructOnScope;
class SDataProviderActivityFilter;
struct FStageDataEntry;

using FDataProviderActivityPtr = TSharedPtr<FDataProviderActivity>;


/**
 *
 */
class SDataProviderActivitiesTableRow : public SMultiColumnTableRow<FDataProviderActivityPtr>
{
	using Super = SMultiColumnTableRow<FDataProviderActivityPtr>;

public:
	SLATE_BEGIN_ARGS(SDataProviderActivitiesTableRow) { }
	SLATE_ARGUMENT(FDataProviderActivityPtr, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwerTableView);
	

private:
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	FText GetTimecode() const;
	FText GetStageName() const;
	FText GetMessageType() const;
	FText GetDescription() const;

private:
	FDataProviderActivityPtr Item;
	FStageInstanceDescriptor Descriptor;
};


/**
 *
 */
class SDataProviderActivities : public SCompoundWidget
{
	using Super = SCompoundWidget;

public:
	SLATE_BEGIN_ARGS(SDataProviderActivities) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SStageMonitorPanel> OwnerPanel, const TWeakPtr<IStageDataCollection>& Collection);
	virtual ~SDataProviderActivities();

	//~ Begin SCompoundWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~End SCompoundWidget interface

	/** Request a full rebuild of the list entries */
	void RequestRebuild();

private:
	TSharedRef<ITableRow> OnGenerateActivityRowWidget(FDataProviderActivityPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnListViewSelectionChanged(FDataProviderActivityPtr InActivity, ESelectInfo::Type SelectInfo);
	void OnNewStageActivity(TSharedPtr<FStageDataEntry> NewActivity);
	void OnActivityFilterChanged();
	void ReloadActivityHistory();
	void OnStageDataCleared();

private:
	TWeakPtr<SStageMonitorPanel> OwnerPanel;
	TWeakPtr<IStageDataCollection> DataCollection;
	TSharedPtr<SListView<FDataProviderActivityPtr>> ActivityList;

	TArray<FDataProviderActivityPtr> Activities;
	TArray<FDataProviderActivityPtr> FilteredActivities;
	TSharedPtr<IStructureDetailsView> StructureDetailsView;
	TSharedPtr<SDataProviderActivityFilter> ActivityFilter;

	bool bRebuildRequested = false;
};

