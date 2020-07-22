// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerItemLabelColumn.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SOverlay.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "ISceneOutlinerMode.h"
#include "SceneOutlinerDragDrop.h"
#include "Widgets/Views/SListView.h"
#include "SortHelper.h"
#include "Widgets/SToolTip.h"


#define LOCTEXT_NAMESPACE "SceneOutlinerItemLabelColumn"

namespace SceneOutliner
{
FName FItemLabelColumn::GetColumnID()
{
	return GetID();
}

SHeaderRow::FColumn::FArguments FItemLabelColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.DefaultLabel(LOCTEXT("ItemLabel_HeaderText", "Label"))
		.FillWidth( 5.0f );
}
const TSharedRef<SWidget> FItemLabelColumn::ConstructRowWidget(FTreeItemRef TreeItem, const STableRow<FTreeItemPtr>& Row)
{
	ISceneOutliner* Outliner = WeakSceneOutliner.Pin().Get();
	check(Outliner);
	return TreeItem->GenerateLabelWidget(*Outliner, Row);
}

void FItemLabelColumn::PopulateSearchStrings( const ITreeItem& Item, TArray< FString >& OutSearchStrings ) const
{
	OutSearchStrings.Add(Item.GetDisplayString());
}

void FItemLabelColumn::SortItems(TArray<FTreeItemPtr>& OutItems, const EColumnSortMode::Type SortMode) const
{
	typedef FSortHelper<int32, FNumericStringWrapper> FSort;

	FSort()
		.Primary([this](const ITreeItem& Item){ return WeakSceneOutliner.Pin()->GetTypeSortPriority(Item); },SortMode)
		.Secondary([](const ITreeItem& Item){ return FNumericStringWrapper(Item.GetDisplayString()); }, 			SortMode)
		.Sort(OutItems);
}
}	// namespace SceneOutliner

#undef LOCTEXT_NAMESPACE
