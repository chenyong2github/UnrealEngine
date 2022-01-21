// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "SlateFwd.h"

class SComboButton;
class SSuggestionTextBox;

/** A custom widget class that provides support for Blueprint namespace entry and/or selection. */
class KISMET_API SBlueprintNamespaceEntry : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnNamespaceSelected, const FString&);
	DECLARE_DELEGATE_OneParam(FOnFilterNamespaceList, TArray<FString>&);

	SLATE_BEGIN_ARGS(SBlueprintNamespaceEntry)
	: _Font(FCoreStyle::Get().GetFontStyle(TEXT("NormalFont")))
	, _AllowTextEntry(true)
	{}
		/** Current namespace value. */
		SLATE_ARGUMENT(FString, CurrentNamespace)

		/** Font color and opacity. */
		SLATE_ATTRIBUTE(FSlateFontInfo, Font)

		/** Allow text input to manually set arbitrary values. */
		SLATE_ARGUMENT(bool, AllowTextEntry)

		/** Allow external code to set custom combo button content. */
		SLATE_NAMED_SLOT(FArguments, ButtonContent)

		/** Called when a valid namespace is either entered or selected. */
		SLATE_EVENT(FOnNamespaceSelected, OnNamespaceSelected)

		/** Called to allow external code to filter out the namespace list. */
		SLATE_EVENT(FOnFilterNamespaceList, OnFilterNamespaceList)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Set the current namespace to the given identifier.
	 * 
	 * @param InNamespace	New namespace identifier. May be an empty string.
	 */
	void SetCurrentNamespace(const FString& InNamespace);

protected:
	void OnTextChanged(const FText& InText);
	void OnTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit);
	void OnShowingSuggestions(const FString& InputText, TArray<FString>& OutSuggestions);
	TSharedRef<SWidget> OnGetNamespaceListMenuContent();
	TSharedRef<ITableRow> OnGenerateRowForNamespaceList(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnNamespaceListFilterTextChanged(const FText& InText);
	void OnNamespaceListSelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo);

	void PopulateNamespaceList();
	void SelectNamespace(const FString& InNamespace);

private:
	FString CurrentNamespace;
	TArray<TSharedPtr<FString>> ListItems;

	TSharedPtr<SComboButton> ComboButton;
	TSharedPtr<SSuggestionTextBox> TextBox;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<TSharedPtr<FString>>> ListView;

	FOnNamespaceSelected OnNamespaceSelected;
	FOnFilterNamespaceList OnFilterNamespaceList;

	static float NamespaceListBorderPadding;
	static float NamespaceListMinDesiredWidth;
};