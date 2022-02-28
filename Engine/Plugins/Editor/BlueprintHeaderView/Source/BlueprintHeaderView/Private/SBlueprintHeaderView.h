// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SComboButton;
class UBlueprint;
class ITableRow;
class STableViewBase;

/** rich text decorators for BlueprintHeaderView Syntax Highlighting */
namespace HeaderViewSyntaxDecorators
{
	extern const FString CommentDecorator;
	extern const FString IdentifierDecorator;
	extern const FString KeywordDecorator;
	extern const FString MacroDecorator;
	extern const FString TypenameDecorator;
}

/** A base class for List Items in the Header View */
struct FHeaderViewListItem : public TSharedFromThis<FHeaderViewListItem>
{
	/** Creates the widget for this list item */
	TSharedRef<SWidget> GenerateWidgetForItem();

	/** Creates a basic list item containing some text */
	static TSharedPtr<FHeaderViewListItem> Create(FString InRawString, FString InRichText);

protected:
	/** Empty base constructor hidden from public */
	FHeaderViewListItem() {};

	FHeaderViewListItem(FString&& InRawString, FString&& InRichText);

	/** 
	 * Formats a string into a C++ comment
	 * @param InComment The string to format as a C++ comment
	 * @param OutRawString The string formatted as a C++ comment
	 * @param OutRichString The string formatted as a C++ comment with rich text decorators for syntax highlighting
	 */
	static void FormatCommentString(FString InComment, FString& OutRawString, FString& OutRichString);

	/** 
	 * returns a string representing the full C++ typename for the given property, 
	 * including template params for container types
	 */
	static FString GetCPPTypenameForProperty(const FProperty* InProperty);

protected:
	/** A rich text representation of the item, including syntax highlighting and errors */
	FString RichTextString;

	/** A raw string representation of the item, used for copying the item */
	FString RawItemString;

};

using FHeaderViewListItemPtr = TSharedPtr<FHeaderViewListItem>;

class SBlueprintHeaderView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintHeaderView)
	{
	}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** Gets the text for the class picker combo button */
	FText GetClassPickerText() const;

	/** Constructs a Blueprint Class picker menu widget */
	TSharedRef<SWidget> GetClassPickerMenuContent();

	/** Callback for class picker menu selecting a blueprint asset */
	void OnAssetSelected(const FAssetData& SelectedAsset);

	/** Generates a row for a given List Item */
	TSharedRef<ITableRow> GenerateRowForItem(FHeaderViewListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable) const;
	
	/** Clears the list and repopulates it with info for the selected Blueprint */
	void RepopulateListView();

	/** Adds items to the list view representing all functions present in the given blueprint */
	void PopulateFunctionItems(const UBlueprint* Blueprint);

	/** Adds items to the list view representing all variables present in the given blueprints */
	void PopulateVariableItems(const UBlueprint* Blueprint);
private:
	/** The blueprint currently being displayed by the header view */
	TWeakObjectPtr<UBlueprint> SelectedBlueprint;

	/** Reference to the Class Picker combo button widget */
	TSharedPtr<SComboButton> ClassPickerComboButton;

	/** Reference to the ListView Widget*/
	TSharedPtr<SListView<FHeaderViewListItemPtr>> ListView;

	/** List Items source */
	TArray<FHeaderViewListItemPtr> ListItems;
};