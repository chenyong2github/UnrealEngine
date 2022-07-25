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
			if (UObjectMixerObjectFilter* Filter = PinnedListModel->GetObjectFilter())
			{
				return Filter;
			}
		}
	}

	return nullptr;
}

FObjectMixerEditorListRow::EObjectMixerEditorListRowType FObjectMixerEditorListRow::GetRowType() const
{
	return RowType;
}

int32 FObjectMixerEditorListRow::GetChildDepth() const
{
	return ChildDepth;
}

void FObjectMixerEditorListRow::SetChildDepth(const int32 InDepth)
{
	ChildDepth = InDepth;
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
	ChildRows.Add(InRow);
}

void FObjectMixerEditorListRow::InsertChildRowAtIndex(const FObjectMixerEditorListRowPtr& InRow,
                                                           const int32 AtIndex)
{
	ChildRows.Insert(InRow, AtIndex);
}

bool FObjectMixerEditorListRow::GetIsTreeViewItemExpanded() const
{
	return bIsTreeViewItemExpanded;
}

void FObjectMixerEditorListRow::SetIsTreeViewItemExpanded(const bool bNewExpanded)
{
	bIsTreeViewItemExpanded = bNewExpanded;
}

bool FObjectMixerEditorListRow::GetShouldFlashOnScrollIntoView() const
{
	return bShouldFlashOnScrollIntoView;
}

void FObjectMixerEditorListRow::SetShouldFlashOnScrollIntoView(const bool bNewShouldFlashOnScrollIntoView)
{
	bShouldFlashOnScrollIntoView = bNewShouldFlashOnScrollIntoView;
}

bool FObjectMixerEditorListRow::GetShouldExpandAllChildren() const
{
	return bShouldExpandAllChildren;
}

void FObjectMixerEditorListRow::SetShouldExpandAllChildren(const bool bNewShouldExpandAllChildren)
{
	bShouldExpandAllChildren = bNewShouldExpandAllChildren;
}

void FObjectMixerEditorListRow::ResetToStartupValueAndSource() const
{

}

bool FObjectMixerEditorListRow::MatchSearchTokensToSearchTerms(
	const TArray<FString> InTokens, ESearchCase::Type InSearchCase)
{
	// If the search is cleared we'll consider the row to pass search
	bool bMatchFound = true;

	if (const TObjectPtr<UObject> Object = GetObject())
	{
		if (CachedSearchTerms.IsEmpty())
		{
			CachedSearchTerms = Object->GetName();

			if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
			{
				CachedSearchTerms = Filter->GetRowDisplayName(Object).ToString();
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

		if (ChildRow->GetRowType() == EObjectMixerEditorListRowType::Group)
		{
			if (ChildRow->MatchSearchTokensToSearchTerms(Tokens))
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
		else
		{
			ChildRow->MatchSearchTokensToSearchTerms(Tokens);
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

bool FObjectMixerEditorListRow::ShouldBeVisible() const
{
	return (bDoesRowMatchSearchTerms && bDoesRowPassFilters) || HasVisibleChildren();
}

EVisibility FObjectMixerEditorListRow::GetDesiredVisibility() const
{
	return ShouldBeVisible() ? EVisibility::Visible : EVisibility::Collapsed;
}

TArray<FObjectMixerEditorListRowPtr> FObjectMixerEditorListRow::GetSelectedTreeViewItems() const
{
	return ListViewPtr.Pin()->GetSelectedTreeViewItems();
}

const FSlateBrush* FObjectMixerEditorListRow::GetObjectIconBrush()
{
	if (GetRowType() != SingleItem)
	{
		return nullptr;
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
			if (AActor* OuterActor = RowObject->GetTypedOuter<AActor>())
			{
				return GetIconForActor(OuterActor);
			}
		
			return FSlateIconFinder::FindIconBrushForClass(RowObject->GetClass(), TEXT("SCS.Component"));
		}
	}

	return nullptr;
}

bool FObjectMixerEditorListRow::GetObjectVisibility()
{
	if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
	{
		return Filter->GetRowEditorVisibility(GetObject());
	}

	return false;
}

void FObjectMixerEditorListRow::SetObjectVisibility(const bool bNewIsVisible)
{
	if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
	{
		Filter->OnSetRowEditorVisibility(GetObject(), bNewIsVisible);
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
