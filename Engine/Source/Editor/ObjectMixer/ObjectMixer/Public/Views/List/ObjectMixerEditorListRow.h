// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Layout/Visibility.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

class SObjectMixerEditorList;
class UObjectMixerObjectFilter;

struct FSlateBrush;

struct FObjectMixerEditorListRow;
typedef TSharedPtr<FObjectMixerEditorListRow> FObjectMixerEditorListRowPtr;

struct OBJECTMIXEREDITOR_API FObjectMixerEditorListRow final : TSharedFromThis<FObjectMixerEditorListRow>
{
	enum EObjectMixerEditorListRowType
	{
		None,
		Group,
		SingleItem
	};

	~FObjectMixerEditorListRow();

	void FlushReferences();
	
	FObjectMixerEditorListRow(
		const TWeakObjectPtr<UObject> InObject, const EObjectMixerEditorListRowType InRowType, 
		const TSharedRef<SObjectMixerEditorList>& InListView, 
		const TWeakPtr<FObjectMixerEditorListRow>& InDirectParentRow)
	: ObjectRef(InObject)
	, RowType(InRowType)
	, ListViewPtr(InListView)
	, DirectParentRow(InDirectParentRow)
	{}

	[[nodiscard]] UObject* GetObject()
	{
		if (ObjectRef.IsValid())
		{
			return ObjectRef.Get();
		}

		return nullptr;
	}

	UObjectMixerObjectFilter* GetObjectFilter() const;

	[[nodiscard]] EObjectMixerEditorListRowType GetRowType() const;

	[[nodiscard]] int32 GetChildDepth() const;
	void SetChildDepth(const int32 InDepth);

	[[nodiscard]] int32 GetSortOrder() const;
	void SetSortOrder(const int32 InNewOrder);

	TWeakPtr<FObjectMixerEditorListRow> GetDirectParentRow() const;
	void SetDirectParentRow(const TWeakPtr<FObjectMixerEditorListRow>& InDirectParentRow);
	
	/* bHasGeneratedChildren must be true to get actual children. */
	[[nodiscard]] const TArray<FObjectMixerEditorListRowPtr>& GetChildRows() const;
	/* bHasGeneratedChildren must be true to get an accurate value. */
	[[nodiscard]] int32 GetChildCount() const;
	void SetChildRows(const TArray<FObjectMixerEditorListRowPtr>& InChildRows);
	void AddToChildRows(const FObjectMixerEditorListRowPtr& InRow);
	void InsertChildRowAtIndex(const FObjectMixerEditorListRowPtr& InRow, const int32 AtIndex = 0);

	[[nodiscard]] bool GetIsTreeViewItemExpanded() const;
	void SetIsTreeViewItemExpanded(const bool bNewExpanded);
	
	[[nodiscard]] bool GetShouldFlashOnScrollIntoView() const;
	void SetShouldFlashOnScrollIntoView(const bool bNewShouldFlashOnScrollIntoView);

	[[nodiscard]] bool GetShouldExpandAllChildren() const;
	void SetShouldExpandAllChildren(const bool bNewShouldExpandAllChildren);

	void ResetToStartupValueAndSource() const;

	/*
	 *Individual members of InTokens will be considered "AnyOf" or "OR" searches. If SearchTerms contains any individual member it will match.
	 *Members will be tested for a space character (" "). If a space is found, a subsearch will be run.
	 *This subsearch will be an "AllOf" or "AND" type search in which all strings, separated by a space, must be found in the search terms.
	 */
	bool MatchSearchTokensToSearchTerms(const TArray<FString> InTokens, ESearchCase::Type InSearchCase = ESearchCase::IgnoreCase);

	/* This overload creates tokens from a string first, then calls ExecuteSearchOnChildNodes(const TArray<FString>& Tokens). */
	void ExecuteSearchOnChildNodes(const FString& SearchString) const;
	void ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const;

	[[nodiscard]] bool GetDoesRowPassFilters() const;
	void SetDoesRowPassFilters(const bool bPass);

	[[nodiscard]] bool GetIsSelected();

	[[nodiscard]] bool ShouldBeVisible() const;
	[[nodiscard]] EVisibility GetDesiredVisibility() const;

	[[nodiscard]] bool HasVisibleChildren() const
	{
		return false;
	}

	[[nodiscard]] TWeakPtr<SObjectMixerEditorList> GetListViewPtr() const
	{
		return ListViewPtr;
	}

	[[nodiscard]] TArray<FObjectMixerEditorListRowPtr> GetSelectedTreeViewItems() const;

	const FSlateBrush* GetObjectIconBrush();

	bool GetObjectVisibility();
	void SetObjectVisibility(const bool bNewIsVisible);

	bool IsThisRowSolo() const;
	void SetThisAsSoloRow();

	void ClearSoloRow();

private:

	TWeakObjectPtr<UObject> ObjectRef;
	EObjectMixerEditorListRowType RowType = SingleItem;
	TArray<FObjectMixerEditorListRowPtr> ChildRows;
	
	TWeakPtr<SObjectMixerEditorList> ListViewPtr;
	
	bool bIsTreeViewItemExpanded = false;
	bool bShouldFlashOnScrollIntoView = false;

	int32 ChildDepth = 0;

	int32 SortOrder = -1;

	FString CachedSearchTerms;

	bool bDoesRowMatchSearchTerms = true;
	bool bDoesRowPassFilters = true;
	
	bool bIsSelected = false;
	TWeakPtr<FObjectMixerEditorListRow> DirectParentRow;

	// Used to expand all children on shift+click.
	bool bShouldExpandAllChildren = false;
};
