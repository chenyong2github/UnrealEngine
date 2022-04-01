// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
template <typename ItemType> class SListView;
class STableViewBase;
class SMVVMViewBindingPanel;
class UMVVMWidgetBlueprintExtension_View;

struct FMVVMViewBindingListEntry;
using FMVVMViewBindingListEntryPtr = TSharedPtr<FMVVMViewBindingListEntry>;

class SMVVMViewBindingListView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMViewBindingListView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SMVVMViewBindingPanel> Owner, UMVVMWidgetBlueprintExtension_View* MVVMExtension);
	~SMVVMViewBindingListView();

	void RequestListRefresh();

	/** Constructs context menu used for right click and dropdown button */
	TSharedPtr<SWidget> OnSourceConstructContextMenu();

private:
	TSharedRef<ITableRow> MakeSourceListViewWidget(FMVVMViewBindingListEntryPtr Entry, const TSharedRef<STableViewBase>& OwnerTable) const;
	void OnSourceListSelectionChanged(FMVVMViewBindingListEntryPtr Entry, ESelectInfo::Type SelectionType) const;

private:
	TWeakPtr<SMVVMViewBindingPanel> BindingPanel;
	TSharedPtr<SListView<FMVVMViewBindingListEntryPtr>> ListView;
	TArray<FMVVMViewBindingListEntryPtr> SourceData;
	TWeakObjectPtr<UMVVMWidgetBlueprintExtension_View> MVVMExtension;
	mutable bool bSelectionChangedGuard = false;
};