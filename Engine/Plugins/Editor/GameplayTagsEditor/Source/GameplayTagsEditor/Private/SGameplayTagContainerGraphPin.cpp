// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGameplayTagContainerGraphPin.h"
#include "GameplayTagPinUtilities.h"

#define LOCTEXT_NAMESPACE "GameplayTagGraphPin"

void SGameplayTagContainerGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGameplayTagGraphPin::Construct(SGameplayTagGraphPin::FArguments(), InGraphPinObj);
}

void SGameplayTagContainerGraphPin::ParseDefaultValueData()
{
	FString TagString = GraphPinObj->GetDefaultAsString();

	FilterString = GameplayTagPinUtilities::ExtractTagFilterStringFromGraphPin(GraphPinObj);

	// This parsing code should be the same as ImportText but there may be older data this handles better
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

TSharedRef<SWidget> SGameplayTagContainerGraphPin::GetEditContent()
{
	EditableContainers.Empty();
	EditableContainers.Add( SGameplayTagWidget::FEditableGameplayTagContainerDatum( GraphPinObj->GetOwningNode(), TagContainer.Get() ) );

	return SNew( SVerticalBox )
		+SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight( 400 )
		[
			SNew( SGameplayTagWidget, EditableContainers )
			.OnTagChanged(this, &SGameplayTagContainerGraphPin::SaveDefaultValueData)
			.TagContainerName( TEXT("SGameplayTagContainerGraphPin") )
			.Visibility( this, &SGraphPin::GetDefaultValueVisibility )
			.MultiSelect(true)
			.Filter(FilterString)
		];
}

void SGameplayTagContainerGraphPin::SaveDefaultValueData()
{	
	RefreshCachedData();

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
