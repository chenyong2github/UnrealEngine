// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "ISceneOutliner.h"
#include "SceneOutlinerPublicTypes.h"
#include "ISceneOutlinerColumn.h"
#include "SSourceControlWidget.h"
#include "ToolMenu.h"

template<typename ItemType> class STableRow;

/** A column for the SceneOutliner that displays the SCC Information */
class FSceneOutlinerActorSCCColumn : public ISceneOutlinerColumn
{

public:
	FSceneOutlinerActorSCCColumn(ISceneOutliner& SceneOutliner) : WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared())) {}

	virtual ~FSceneOutlinerActorSCCColumn() {}

	static FName GetID() { return FSceneOutlinerBuiltInColumnTypes::SourceControl(); }

	bool AddSourceControlMenuOptions(UToolMenu* Menu, TArray<FSceneOutlinerTreeItemPtr> InSelectedItems);

	//////////////////////////////////////////////////////////////////////////
	// Begin ISceneOutlinerColumn Implementation

	virtual FName GetColumnID() override;

	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;

	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;

	virtual bool SupportsSorting() const override { return false; }

	// End ISceneOutlinerColumn Implementation
	//////////////////////////////////////////////////////////////////////////
private:
	const FSlateBrush* GetHeaderIcon() const;

	bool CanExecuteSourceControlActions() const;
	void CacheCanExecuteVars();
	bool CanExecuteSCCCheckOut() const;
	bool CanExecuteSCCCheckIn() const;
	bool CanExecuteSCCHistory() const;
	bool CanExecuteSCCRevert() const;
	bool CanExecuteSCCRefresh() const;
	void FillSourceControlSubMenu(UToolMenu* Menu);
	void GetSelectedPackageNames(TArray<FString>& OutPackageNames) const;
	void GetSelectedPackages(TArray<UPackage*>& OutPackages) const;
	void ExecuteSCCRefresh();
	void ExecuteSCCCheckOut();
	void ExecuteSCCCheckIn();
	void ExecuteSCCHistory();
	void ExecuteSCCRevert();

	TWeakPtr<ISceneOutliner> WeakSceneOutliner;

	TArray<FSceneOutlinerTreeItemPtr> SelectedItems;

	TMap<FSceneOutlinerTreeItemPtr, TSharedRef<SSourceControlWidget>> ItemWidgets;

	bool bCanExecuteSCCCheckOut = false;
	bool bCanExecuteSCCCheckIn = false;
	bool bCanExecuteSCCHistory = false;
	bool bCanExecuteSCCRevert = false;
};
