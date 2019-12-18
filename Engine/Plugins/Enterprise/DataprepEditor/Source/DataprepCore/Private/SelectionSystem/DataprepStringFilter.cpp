// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SelectionSystem/DataprepStringFilter.h"

#include "DataprepCoreLogCategory.h"
#include "SelectionSystem/DataprepSelectionSystemUtils.h"
#include "SelectionSystem/DataprepStringFetcher.h"

bool UDataprepStringFilter::Filter(const FString& String) const
{
	switch ( StringMatchingCriteria )
	{
		case EDataprepStringMatchType::Contains:
			return String.Contains( UserString );
			break;
		case EDataprepStringMatchType::ExactMatch:
			return String.Equals( UserString );
			break;
		case EDataprepStringMatchType::MatchesWildcard:
			return String.MatchesWildcard( UserString );
			break;
		default:
			check( false );
			break;
	}

	return false;
}

TArray<UObject*> UDataprepStringFilter::FilterObjects(const TArray<UObject *>& Objects) const
{
	if ( StringFetcher )
	{
		return DataprepSelectionSystemUtils::FilterObjects< UDataprepStringFilter, UDataprepStringFetcher, FString >( *this, *StringFetcher, Objects );
	}

	ensure(false);
	UE_LOG(LogDataprepCore, Error, TEXT("UDataprepStringFilter::FilterObjects: There was no Fetcher"));
	return {};
}

TSubclassOf<UDataprepFetcher> UDataprepStringFilter::GetAcceptedFetcherClass() const
{
	return UDataprepStringFetcher::StaticClass();
}

void UDataprepStringFilter::SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass)
{
	UClass* NewFetcherClass = FetcherClass;

	if ( NewFetcherClass && NewFetcherClass->IsChildOf( GetAcceptedFetcherClass() ) )
	{
		UClass* OldFetcherClass = StringFetcher ? StringFetcher->GetClass() : nullptr;
		if ( NewFetcherClass != OldFetcherClass )
		{
			Modify();
			StringFetcher = NewObject< UDataprepStringFetcher >( this, NewFetcherClass, NAME_None, RF_Transactional );
		}
	}
	else
	{
		ensure( false );
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepStringFilter::SetFetcher: The Fetcher Class is not compatible") );
	}
}

UDataprepFetcher* UDataprepStringFilter::GetFetcher() const
{
	return StringFetcher;
}

FText UDataprepStringFilter::GetFilterCategoryText() const
{
	return NSLOCTEXT("DataprepSringFilter", "StringFilterCategory", "String");
}

EDataprepStringMatchType UDataprepStringFilter::GetStringMatchingCriteria() const
{
	return StringMatchingCriteria;
}

FString UDataprepStringFilter::GetUserString() const
{
	return UserString;
}

void UDataprepStringFilter::SetStringMatchingCriteria(EDataprepStringMatchType InStringMatchingCriteria)
{
	if ( StringMatchingCriteria != InStringMatchingCriteria )
	{
		Modify();
		StringMatchingCriteria = InStringMatchingCriteria;
	}
}

void UDataprepStringFilter::SetUserString(FString InUserString)
{
	if ( UserString != InUserString )
	{
		Modify();
		UserString = InUserString;
	}
}
