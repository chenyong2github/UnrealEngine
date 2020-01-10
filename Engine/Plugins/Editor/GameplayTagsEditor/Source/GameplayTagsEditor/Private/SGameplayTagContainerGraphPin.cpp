// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGameplayTagContainerGraphPin.h"
#include "Widgets/Input/SComboButton.h"
#include "GameplayTagsModule.h"
#include "Widgets/Layout/SScaleBox.h"
#include "GameplayTagPinUtilities.h"

#define LOCTEXT_NAMESPACE "GameplayTagGraphPin"

void SGameplayTagContainerGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	TagContainer = MakeShareable( new FGameplayTagContainer() );
	SGraphPin::Construct( SGraphPin::FArguments(), InGraphPinObj );
}

TSharedRef<SWidget>	SGameplayTagContainerGraphPin::GetDefaultValueWidget()
{
	ParseDefaultValueData();

	//Create widget
	return SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew( ComboButton, SComboButton )
			.OnGetMenuContent(this, &SGameplayTagContainerGraphPin::GetListContent)
			.ContentPadding( FMargin( 2.0f, 2.0f ) )
			.Visibility( this, &SGraphPin::GetDefaultValueVisibility )
			.ButtonContent()
			[
				SNew( STextBlock )
				.Text( LOCTEXT("GameplayTagWidget_Edit", "Edit") )
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SelectedTags()
		];
}

void SGameplayTagContainerGraphPin::ParseDefaultValueData()
{
	FString TagString = GraphPinObj->GetDefaultAsString();

	FilterString = GameplayTagPinUtilities::ExtractTagFilterStringFromGraphPin(GraphPinObj);

	if (TagString.StartsWith(TEXT("("), ESearchCase::CaseSensitive) && TagString.EndsWith(TEXT(")"), ESearchCase::CaseSensitive))
	{
		TagString.LeftChopInline(1, false);
		TagString.RightChopInline(1, false);

		TagString.Split(TEXT("="), nullptr, &TagString, ESearchCase::CaseSensitive);

		TagString.LeftChopInline(1, false);
		TagString.RightChopInline(1, false);

		FString ReadTag;
		FString Remainder;

		while (TagString.Split(TEXT(","), &ReadTag, &Remainder, ESearchCase::CaseSensitive))
		{
			ReadTag.Split(TEXT("="), NULL, &ReadTag, ESearchCase::CaseSensitive);
			if (ReadTag.EndsWith(TEXT(")"), ESearchCase::CaseSensitive))
			{
				ReadTag.LeftChopInline(1, false);
				if (ReadTag.StartsWith(TEXT("\""), ESearchCase::CaseSensitive) && ReadTag.EndsWith(TEXT("\""), ESearchCase::CaseSensitive))
				{
					ReadTag.LeftChopInline(1, false);
					ReadTag.RightChopInline(1, false);
				}
			}
			TagString = Remainder;
			FGameplayTag GameplayTag = FGameplayTag::RequestGameplayTag(FName(*ReadTag));
			TagContainer->AddTag(GameplayTag);
		}
		if (Remainder.IsEmpty())
		{
			Remainder = TagString;
		}
		if (!Remainder.IsEmpty())
		{
			Remainder.Split(TEXT("="), nullptr, &Remainder, ESearchCase::CaseSensitive);
			if (Remainder.EndsWith(TEXT(")"), ESearchCase::CaseSensitive))
			{
				Remainder.LeftChopInline(1, false);
				if (Remainder.StartsWith(TEXT("\""), ESearchCase::CaseSensitive) && Remainder.EndsWith(TEXT("\""), ESearchCase::CaseSensitive))
				{
					Remainder.LeftChopInline(1, false);
					Remainder.RightChopInline(1, false);
				}
			}
			FGameplayTag GameplayTag = FGameplayTag::RequestGameplayTag(FName(*Remainder));
			TagContainer->AddTag(GameplayTag);
		}
	}
}

TSharedRef<SWidget> SGameplayTagContainerGraphPin::GetListContent()
{
	EditableContainers.Empty();
	EditableContainers.Add( SGameplayTagWidget::FEditableGameplayTagContainerDatum( GraphPinObj->GetOwningNode(), TagContainer.Get() ) );

	return SNew( SVerticalBox )
		+SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight( 400 )
		[
			SNew( SGameplayTagWidget, EditableContainers )
			.OnTagChanged(this, &SGameplayTagContainerGraphPin::RefreshTagList)
			.TagContainerName( TEXT("SGameplayTagContainerGraphPin") )
			.Visibility( this, &SGraphPin::GetDefaultValueVisibility )
			.Filter(FilterString)
		];
}

TSharedRef<SWidget> SGameplayTagContainerGraphPin::SelectedTags()
{
	RefreshTagList();

	SAssignNew( TagListView, SListView<TSharedPtr<FString>> )
		.ListItemsSource(&TagNames)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &SGameplayTagContainerGraphPin::OnGenerateRow);

	return TagListView->AsShared();
}

TSharedRef<ITableRow> SGameplayTagContainerGraphPin::OnGenerateRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew( STableRow< TSharedPtr<FString> >, OwnerTable )
		[
			SNew(STextBlock) .Text( FText::FromString(*Item.Get()) )
		];
}

void SGameplayTagContainerGraphPin::RefreshTagList()
{	
	// Clear the list
	TagNames.Empty();

	// Add tags to list
	if (TagContainer.IsValid())
	{
		for (auto It = TagContainer->CreateConstIterator(); It; ++It)
		{
			FString TagName = It->ToString();
			TagNames.Add( MakeShareable( new FString( TagName ) ) );
		}
	}

	// Refresh the slate list
	if( TagListView.IsValid() )
	{
		TagListView->RequestListRefresh();
	}

	// Set Pin Data
	FString TagContainerString = TagContainer->ToString();
	FString CurrentDefaultValue = GraphPinObj->GetDefaultAsString();
	if (CurrentDefaultValue.IsEmpty())
	{
		CurrentDefaultValue = FString(TEXT("(GameplayTags=)"));
	}
	if (!CurrentDefaultValue.Equals(TagContainerString))
	{
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, TagContainerString);
	}
}

#undef LOCTEXT_NAMESPACE
