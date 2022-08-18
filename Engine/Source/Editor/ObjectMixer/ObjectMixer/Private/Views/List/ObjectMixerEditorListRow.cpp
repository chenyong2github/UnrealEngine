// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/ObjectMixerEditorListRow.h"

#include "Algo/AllOf.h"
#include "ClassIconFinder.h"
#include "GameFramework/Actor.h"
#include "Styling/SlateIconFinder.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/List/SObjectMixerEditorList.h"

FObjectMixerEditorListRow::~FObjectMixerEditorListRow()
{
	FlushReferences();
}

void FObjectMixerEditorListRow::FlushReferences()
{
	if (ChildRows.Num())
	{
		ChildRows.Empty();
	}
}

UObjectMixerObjectFilter* FObjectMixerEditorListRow::GetObjectFilter() const
{
	if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = GetListViewPtr().Pin())
	{
		if (const TSharedPtr<FObjectMixerEditorList> PinnedListModel = PinnedListView->GetListModelPtr().Pin())
		{
			if (const TSharedPtr<FObjectMixerEditorMainPanel> PinnedMainPanel = PinnedListModel->GetMainPanelModel().Pin())
			{
				if (UObjectMixerObjectFilter* Filter = PinnedMainPanel->GetObjectFilter())
				{
					return Filter;
				}
			}
		}
	}

	return nullptr;
}

bool FObjectMixerEditorListRow::IsObjectRefInSelectedCategories() const
{
	check (ListViewPtr.IsValid());
	
	if (RowType != EObjectMixerEditorListRowType::None && RowType != EObjectMixerEditorListRowType::Folder)
	{
		if (const TSharedPtr<FObjectMixerEditorList> ListModel = ListViewPtr.Pin()->GetListModelPtr().Pin())
		{
			if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = ListModel->GetMainPanelModel().Pin())
			{
				const TSet<FName>& CategorySelection = MainPanel->GetCurrentCategorySelection();

				if (CategorySelection.Num() == 0)
				{
					return true;
				}

				const TSet<FName>& ObjectAssignedCategories = MainPanel->GetCategoriesForObject(GetObject());

				const TSet<FName> Intersection = CategorySelection.Intersect(ObjectAssignedCategories);

				return Intersection.Num() > 0;
			}
		}
	}
	return false;
}

FObjectMixerEditorListRow::EObjectMixerEditorListRowType FObjectMixerEditorListRow::GetRowType() const
{
	return RowType;
}

int32 FObjectMixerEditorListRow::GetSortOrder() const
{
	return SortOrder;
}

void FObjectMixerEditorListRow::SetSortOrder(const int32 InNewOrder)
{
	SortOrder = InNewOrder;
}

TWeakPtr<FObjectMixerEditorListRow> FObjectMixerEditorListRow::GetDirectParentRow() const
{
	return DirectParentRow;
}

void FObjectMixerEditorListRow::SetDirectParentRow(
	const TWeakPtr<FObjectMixerEditorListRow>& InDirectParentRow)
{
	DirectParentRow = InDirectParentRow;
}

const TArray<FObjectMixerEditorListRowPtr>& FObjectMixerEditorListRow::GetChildRows() const
{
	return ChildRows;
}

int32 FObjectMixerEditorListRow::GetChildCount() const
{
	return ChildRows.Num();
}

void FObjectMixerEditorListRow::SetChildRows(const TArray<FObjectMixerEditorListRowPtr>& InChildRows)
{
	ChildRows = InChildRows;
}

void FObjectMixerEditorListRow::AddToChildRows(const FObjectMixerEditorListRowPtr& InRow)
{
	InRow->SetDirectParentRow(SharedThis(this));
	ChildRows.AddUnique(InRow);
	ChildRows.StableSort(SObjectMixerEditorList::SortByTypeThenName);
}

void FObjectMixerEditorListRow::InsertChildRowAtIndex(const FObjectMixerEditorListRowPtr& InRow,
                                                           const int32 AtIndex)
{
	ChildRows.Insert(InRow, AtIndex);
}

bool FObjectMixerEditorListRow::GetIsTreeViewItemExpanded()
{
	return GetListViewPtr().Pin()->IsTreeViewItemExpanded(GetAsShared());
}

void FObjectMixerEditorListRow::SetIsTreeViewItemExpanded(const bool bNewExpanded)
{
	GetListViewPtr().Pin()->SetTreeViewItemExpanded(GetAsShared(), bNewExpanded);
}

bool FObjectMixerEditorListRow::GetShouldExpandAllChildren() const
{
	return bShouldExpandAllChildren;
}

void FObjectMixerEditorListRow::SetShouldExpandAllChildren(const bool bNewShouldExpandAllChildren)
{
	bShouldExpandAllChildren = bNewShouldExpandAllChildren;
}

bool FObjectMixerEditorListRow::MatchSearchTokensToSearchTerms(
	const TArray<FString> InTokens, ESearchCase::Type InSearchCase)
{
	// If the search is cleared we'll consider the row to pass search
	bool bMatchFound = true;
	
	if (CachedSearchTerms.IsEmpty())
	{
		CachedSearchTerms = GetDisplayNameOverride().ToString() + " ";

		if (const TObjectPtr<UObject> Object = GetObject())
		{			
			if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
			{
				CachedSearchTerms += Filter->GetRowDisplayName(Object, GetTreeViewMode()).ToString();
			}
		}
	}

	// Match any
	for (const FString& Token : InTokens)
	{
		// Match all of these
		const FString SpaceDelimiter = " ";
		TArray<FString> OutSpacedArray;
		if (Token.Contains(SpaceDelimiter) && Token.ParseIntoArray(OutSpacedArray, *SpaceDelimiter, true) > 1)
		{
			bMatchFound = Algo::AllOf(OutSpacedArray, [this, InSearchCase](const FString& Comparator)
			{
				return CachedSearchTerms.Contains(Comparator, InSearchCase);
			});
		}
		else
		{
			bMatchFound = CachedSearchTerms.Contains(Token, InSearchCase);
		}

		if (bMatchFound)
		{
			break;
		}
	}

	bDoesRowMatchSearchTerms = bMatchFound;

	return bMatchFound;
}

void FObjectMixerEditorListRow::ExecuteSearchOnChildNodes(const FString& SearchString) const
{
	TArray<FString> Tokens;

	SearchString.ParseIntoArray(Tokens, TEXT(" "), true);

	ExecuteSearchOnChildNodes(Tokens);
}

void FObjectMixerEditorListRow::ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const
{
	for (const FObjectMixerEditorListRowPtr& ChildRow : GetChildRows())
	{
		if (!ensure(ChildRow.IsValid()))
		{
			continue;
		}

		const bool bMatch = ChildRow->MatchSearchTokensToSearchTerms(Tokens);;

		if (ChildRow->GetChildCount() > 0)
		{
			if (bMatch)
			{
				// If the group name matches then we pass an empty string to search child nodes since we want them all to be visible
				ChildRow->ExecuteSearchOnChildNodes("");
			}
			else
			{
				// Otherwise we iterate over all child nodes to determine which should and should not be visible
				ChildRow->ExecuteSearchOnChildNodes(Tokens);
			}
		}
	}
}

bool FObjectMixerEditorListRow::GetDoesRowPassFilters() const
{
	return bDoesRowPassFilters;
}

void FObjectMixerEditorListRow::SetDoesRowPassFilters(const bool bPass)
{
	bDoesRowPassFilters = bPass;
}

bool FObjectMixerEditorListRow::GetIsSelected()
{
	check (ListViewPtr.IsValid());

	return ListViewPtr.Pin()->IsTreeViewItemSelected(SharedThis(this));
}

bool FObjectMixerEditorListRow::ShouldRowWidgetBeVisible() const
{
	return (bDoesRowMatchSearchTerms && bDoesRowPassFilters) || HasVisibleChildRowWidgets();
}

EVisibility FObjectMixerEditorListRow::GetDesiredRowWidgetVisibility() const
{
	return ShouldRowWidgetBeVisible() ? EVisibility::Visible : EVisibility::Collapsed;
}

bool FObjectMixerEditorListRow::HasVisibleChildRowWidgets() const
{
	if (ChildRows.Num() == 0)
	{
		return false;
	}

	for (const auto& ChildRow : ChildRows)
	{
		if (ChildRow->ShouldRowWidgetBeVisible())
		{
			return true;
		}
	}

	return false;
}

FText FObjectMixerEditorListRow::GetDisplayName()
{
	if (!GetDisplayNameOverride().IsEmpty())
	{
		return GetDisplayNameOverride();
	}

	if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
	{
		if (const TObjectPtr<UObject> Object = GetObject())
		{
			return Filter->GetRowDisplayName(Object, GetTreeViewMode());
		}
	}

	return FText::GetEmpty();
}

EObjectMixerTreeViewMode FObjectMixerEditorListRow::GetTreeViewMode()
{
	const TSharedPtr<SObjectMixerEditorList> PinnedListModel = GetListViewPtr().Pin();
	check(PinnedListModel);
	
	return PinnedListModel->GetTreeViewMode();
}

TArray<FObjectMixerEditorListRowPtr> FObjectMixerEditorListRow::GetSelectedTreeViewItems() const
{
	return ListViewPtr.Pin()->GetSelectedTreeViewItems();
}

const FSlateBrush* FObjectMixerEditorListRow::GetObjectIconBrush()
{
	if (GetRowType() == EObjectMixerEditorListRowType::None)
	{
		return nullptr;
	}

	if (GetRowType() == EObjectMixerEditorListRowType::Folder)
	{
		if (GetIsTreeViewItemExpanded() && ChildRows.Num())
		{
			return FAppStyle::Get().GetBrush(TEXT("SceneOutliner.FolderOpen"));
		}
		
		return FAppStyle::Get().GetBrush(TEXT("SceneOutliner.FolderClosed"));
	}
	
	if (UObject* RowObject = GetObject())
	{
		auto GetIconForActor = [](AActor* InActor)
		{
			FName IconName = InActor->GetCustomIconName();
			if (IconName == NAME_None)
			{
				IconName = InActor->GetClass()->GetFName();
			}

			return FClassIconFinder::FindIconForActor(InActor);
		};

		if (AActor* AsActor = Cast<AActor>(RowObject))
		{
			return GetIconForActor(AsActor);
		}
	
		if (RowObject->IsA(UActorComponent::StaticClass()))
		{		
			return FSlateIconFinder::FindIconBrushForClass(RowObject->GetClass(), TEXT("SCS.Component"));
		}
	}

	return nullptr;
}

bool FObjectMixerEditorListRow::GetObjectVisibility()
{
	if (GetRowType() == EObjectMixerEditorListRowType::Folder)
	{
		// If any child returns true, the folder returns true
		for (const FObjectMixerEditorListRowPtr& Child : GetChildRows())
		{
			if (Child->GetObjectVisibility())
			{
				return true;
			}
		}

		// The folder's visibility is only returned false if all children also return false
		return false;
	}
	
	if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
	{
		return Filter->GetRowEditorVisibility(GetObject(), GetTreeViewMode());
	}

	return false;
}

void FObjectMixerEditorListRow::SetObjectVisibility(const bool bNewIsVisible, const bool bIsRecursive)
{
	if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
	{
		Filter->OnSetRowEditorVisibility(GetObject(), bNewIsVisible, GetTreeViewMode());

		if (bIsRecursive)
		{
			for (const FObjectMixerEditorListRowPtr& Child : GetChildRows())
			{
				Child->SetObjectVisibility(bNewIsVisible, true);
			}
		}
	}
}

bool FObjectMixerEditorListRow::IsThisRowSolo() const
{
	return GetListViewPtr().Pin()->GetSoloRow().HasSameObject(this);
}

void FObjectMixerEditorListRow::SetThisAsSoloRow()
{
	GetListViewPtr().Pin()->SetSoloRow(SharedThis(this));
}

void FObjectMixerEditorListRow::ClearSoloRow()
{
	GetListViewPtr().Pin()->ClearSoloRow();
}

FObjectMixerEditorListRowPtr FObjectMixerEditorListRow::GetAsShared()
{
	return SharedThis(this);
}
