// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGameplayTagGraphPin.h"
#include "GameplayTagPinUtilities.h"

#define LOCTEXT_NAMESPACE "GameplayTagGraphPin"

void SGameplayTagGraphPin::Construct( const FArguments& InArgs, UEdGraphPin* InGraphPinObj )
{
	TagContainer = MakeShareable( new FGameplayTagContainer() );
	SGraphPin::Construct( SGraphPin::FArguments(), InGraphPinObj );
}

void SGameplayTagGraphPin::ParseDefaultValueData()
{
	FString TagString = GraphPinObj->GetDefaultAsString();

	FilterString = GameplayTagPinUtilities::ExtractTagFilterStringFromGraphPin(GraphPinObj);

	if (TagString.StartsWith(TEXT("("), ESearchCase::CaseSensitive) && TagString.EndsWith(TEXT(")"), ESearchCase::CaseSensitive))
	{
		TagString.LeftChopInline(1, false);
		TagString.RightChopInline(1, false);
		TagString.Split(TEXT("="), nullptr, &TagString, ESearchCase::CaseSensitive);
		if (TagString.StartsWith(TEXT("\""), ESearchCase::CaseSensitive) && TagString.EndsWith(TEXT("\""), ESearchCase::CaseSensitive))
		{
			TagString.LeftChopInline(1, false);
			TagString.RightChopInline(1, false);
		}
	}

	if (!TagString.IsEmpty())
	{
		FGameplayTag GameplayTag = FGameplayTag::RequestGameplayTag(FName(*TagString));
		TagContainer->AddTag(GameplayTag);
	}
}

TSharedRef<SWidget> SGameplayTagGraphPin::GetEditContent()
{
	EditableContainers.Empty();
	EditableContainers.Add( SGameplayTagWidget::FEditableGameplayTagContainerDatum( GraphPinObj->GetOwningNode(), TagContainer.Get() ) );

	return SNew( SVerticalBox )
		+SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight( 400 )
		[
			SNew( SGameplayTagWidget, EditableContainers )
			.OnTagChanged( this, &SGameplayTagGraphPin::SaveDefaultValueData)
			.TagContainerName( TEXT("SGameplayTagGraphPin") )
			.Visibility( this, &SGraphPin::GetDefaultValueVisibility )
			.MultiSelect(false)
			.Filter(FilterString)
		];
}

TSharedRef<SWidget> SGameplayTagGraphPin::GetDescriptionContent()
{
	RefreshCachedData();

	SAssignNew( TagListView, SListView<TSharedPtr<FString>> )
		.ListItemsSource(&TagNames)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &SGameplayTagGraphPin::OnGenerateRow);

	return TagListView->AsShared();
}

TSharedRef<ITableRow> SGameplayTagGraphPin::OnGenerateRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew( STableRow< TSharedPtr<FString> >, OwnerTable )
		[
			SNew(STextBlock) .Text( FText::FromString(*Item.Get()) )
		];
}

void SGameplayTagGraphPin::RefreshCachedData()
{
	// Clear the list
	TagNames.Empty();

	// Add tags to list
	FString TagName;
	if (TagContainer.IsValid())
	{
		for (auto It = TagContainer->CreateConstIterator(); It; ++It)
		{
			TagName = It->ToString();
			TagNames.Add(MakeShareable(new FString(TagName)));
		}
	}

	// Refresh the slate list
	if (TagListView.IsValid())
	{
		TagListView->RequestListRefresh();
	}
}

void SGameplayTagGraphPin::SaveDefaultValueData()
{	
	RefreshCachedData();

	// Set Pin Data
	FString TagString;

	if (TagNames.Num() > 0 && TagNames[0].IsValid())
	{
		TagString = TEXT("(");
		TagString += TEXT("TagName=\"");
		TagString += *TagNames[0].Get();
		TagString += TEXT("\"");
		TagString += TEXT(")");
	}
	FString CurrentDefaultValue = GraphPinObj->GetDefaultAsString();
	if (CurrentDefaultValue.IsEmpty() || CurrentDefaultValue == TEXT("(TagName=\"\")"))
	{
		CurrentDefaultValue = FString(TEXT(""));
	}
	if (!CurrentDefaultValue.Equals(TagString))
	{
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, TagString);
	}
}

#undef LOCTEXT_NAMESPACE
