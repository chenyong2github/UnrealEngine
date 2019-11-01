// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SelectionSystem/DataprepStringsArrayFilter.h"

#include "DataprepCoreLogCategory.h"
#include "SelectionSystem/DataprepSelectionSystemUtils.h"
#include "SelectionSystem/DataprepStringsArrayFetcher.h"

bool UDataprepStringsArrayFilter::Filter(const TArray<FString>& StringArray) const
{
	bool bResult = false;

	for (int Index = 0; Index < StringArray.Num(); ++Index)
	{
		switch (StringMatchingCriteria)
		{
			case EDataprepStringMatchType::Contains:
				bResult = StringArray[Index].Equals(UserString);
				break;
			case EDataprepStringMatchType::ExactMatch:
				bResult = StringArray[Index].Contains(UserString);
				break;
			case EDataprepStringMatchType::MatchesWildcard:
				bResult = StringArray[Index].MatchesWildcard(UserString);
				break;
		}

		if (bResult)
		{
			break;
		}
	}

	return bResult;
}

TArray<UObject*> UDataprepStringsArrayFilter::FilterObjects(const TArray<UObject *>& Objects) const
{
	if ( StringsArrayFetcher )
	{
		return DataprepSelectionSystemUtils::FilterObjects< UDataprepStringsArrayFilter, UDataprepStringsArrayFetcher, TArray<FString> >( *this, *StringsArrayFetcher, Objects );
	}

	ensure(false);
	UE_LOG(LogDataprepCore, Error, TEXT("UDataprepStringsArrayFilter::FilterObjects: There was no Fetcher"));
	return {};
}

TSubclassOf<UDataprepFetcher> UDataprepStringsArrayFilter::GetAcceptedFetcherClass() const
{
	return UDataprepStringsArrayFetcher::StaticClass();
}

void UDataprepStringsArrayFilter::SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass)
{
	UClass* NewFetcherClass = FetcherClass;

	if ( NewFetcherClass && NewFetcherClass->IsChildOf( GetAcceptedFetcherClass() ) )
	{
		UClass* OldFetcherClass = StringsArrayFetcher ? StringsArrayFetcher->GetClass() : nullptr;
		if ( NewFetcherClass != OldFetcherClass )
		{
			Modify();
			StringsArrayFetcher = NewObject< UDataprepStringsArrayFetcher >( this, NewFetcherClass, NAME_None, RF_Transactional );
		}
	}
	else
	{
		ensure( false );
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepStringFilter::SetFetcher: The Fetcher Class is not compatible") );
	}
}

UDataprepFetcher* UDataprepStringsArrayFilter::GetFetcher() const
{
	return StringsArrayFetcher;
}

FText UDataprepStringsArrayFilter::GetFilterCategoryText() const
{
	return NSLOCTEXT("DataprepSringsArrayFilter", "StringFilterCategory", "String");
}

EDataprepStringMatchType UDataprepStringsArrayFilter::GetStringMatchingCriteria() const
{
	return StringMatchingCriteria;
}

FString UDataprepStringsArrayFilter::GetUserString() const
{
	return UserString;
}

void UDataprepStringsArrayFilter::SetStringMatchingCriteria(EDataprepStringMatchType InStringMatchingCriteria)
{
	if (StringMatchingCriteria != InStringMatchingCriteria)
	{
		Modify();
		StringMatchingCriteria = InStringMatchingCriteria;
	}
}

void UDataprepStringsArrayFilter::SetUserString(FString InUserString)
{
	if (UserString != InUserString)
	{
		Modify();
		UserString = InUserString;
	}
}
