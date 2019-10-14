// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SelectionSystem/DataprepBoolFilter.h"

#include "DataprepCoreLogCategory.h"
#include "SelectionSystem/DataprepSelectionSystemUtils.h"
#include "SelectionSystem/DataprepBoolFetcher.h"

bool UDataprepBoolFilter::Filter(const bool bResult) const
{
	// Simply exist to reuse the templated function in FilterObjects
	return bResult;
}

TArray<UObject*> UDataprepBoolFilter::FilterObjects(const TArray<UObject *>& Objects) const
{
	if ( BoolFetcher )
	{
		return DataprepSelectionSystemUtils::FilterObjects< UDataprepBoolFilter, UDataprepBoolFetcher, bool >( *this, * BoolFetcher, Objects );
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepBoolFilter::FilterObjects: There was no Fetcher") );
	return {};
}

FText UDataprepBoolFilter::GetFilterCategoryText() const
{
	return NSLOCTEXT("DataprepBoolFilter", "BoolFilterCategory", "Condition");
}

TSubclassOf<UDataprepFetcher> UDataprepBoolFilter::GetAcceptedFetcherClass() const
{
	return UDataprepBoolFetcher::StaticClass();
}

void UDataprepBoolFilter::SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass)
{
		UClass* NewFetcherClass = FetcherClass;

	if ( NewFetcherClass && NewFetcherClass->IsChildOf( GetAcceptedFetcherClass() ) )
	{
		UClass* OldFetcherClass = BoolFetcher ? BoolFetcher->GetClass() : nullptr;
		if ( NewFetcherClass != OldFetcherClass )
		{
			Modify();
			BoolFetcher = NewObject< UDataprepBoolFetcher >( this, NewFetcherClass, NAME_None, RF_Transactional );
		}
	}
	else
	{
		ensure( false );
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepBoolFilter::SetFetcher: The Fetcher Class is not compatible") );
	}
}

UDataprepFetcher* UDataprepBoolFilter::GetFetcher() const
{
	return BoolFetcher;
}
