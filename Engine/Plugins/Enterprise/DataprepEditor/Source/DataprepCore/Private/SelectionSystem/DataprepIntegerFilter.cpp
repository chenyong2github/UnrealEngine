// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelectionSystem/DataprepIntegerFilter.h"

#include "DataprepCoreLogCategory.h"
#include "SelectionSystem/DataprepSelectionSystemUtils.h"
#include "SelectionSystem/DataprepIntegerFetcher.h"

bool UDataprepIntegerFilter::Filter(int Integer) const
{
	switch ( IntegerMatchingCriteria )
	{
	case EDataprepIntegerMatchType::LessThan:
		return Integer < EqualValue;
		break;
	case EDataprepIntegerMatchType::GreatherThan:
		return Integer > EqualValue;
		break;
	case EDataprepIntegerMatchType::IsEqual:
		return Integer == EqualValue;
		break;
	case EDataprepIntegerMatchType::InBetween:
		return Integer >= FromValue &&  Integer <= ToValue;
		break;
	default:
		check( false );
		break;
	}

	return false;
}

TArray<UObject*> UDataprepIntegerFilter::FilterObjects(const TArray<UObject*>& Objects) const
{
	if ( IntFetcher )
	{
		return DataprepSelectionSystemUtils::FilterObjects< UDataprepIntegerFilter, UDataprepIntegerFetcher, float >( *this, *IntFetcher, Objects );
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepIntegerFilter::FilterObjects: There was no Fetcher") );
	return {};
}

FText UDataprepIntegerFilter::GetFilterCategoryText() const
{
	return NSLOCTEXT("DataprepIntegerFilter", "IntegerFilterCategory", "Integer");
}

TSubclassOf<UDataprepFetcher> UDataprepIntegerFilter::GetAcceptedFetcherClass() const
{
	return UDataprepIntegerFetcher::StaticClass();
}

void UDataprepIntegerFilter::SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass)
{
	UClass* NewFetcherClass = FetcherClass;
	if ( NewFetcherClass && NewFetcherClass->IsChildOf( GetAcceptedFetcherClass() ) )
	{
		UClass* OldFetcherClass = IntFetcher ? IntFetcher->GetClass() : nullptr;
		if ( NewFetcherClass != OldFetcherClass )
		{
			Modify();
			IntFetcher = NewObject< UDataprepIntegerFetcher >( this, NewFetcherClass, NAME_None, RF_Transactional );
		}
	}
	else
	{
		ensure( false );
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepIntegerFilter::SetFetcher: The Fetcher Class is not compatible") );
	}
}

const UDataprepFetcher* UDataprepIntegerFilter::GetFetcherImplementation() const
{
	return IntFetcher;
}

EDataprepIntegerMatchType UDataprepIntegerFilter::GetIntegerMatchingCriteria() const
{
	return IntegerMatchingCriteria;
}

int UDataprepIntegerFilter::GetEqualValue() const
{
	return EqualValue;
}

int UDataprepIntegerFilter::GetFromValue() const
{
	return FromValue;
}

int UDataprepIntegerFilter::GetToValue() const
{
	return ToValue;
}

void UDataprepIntegerFilter::SetIntegerMatchingCriteria(EDataprepIntegerMatchType InIntegerMatchingCriteria)
{
	if ( IntegerMatchingCriteria != InIntegerMatchingCriteria )
	{
		Modify();
		IntegerMatchingCriteria = InIntegerMatchingCriteria;
	}
}

void UDataprepIntegerFilter::SetEqualValue(int InValue)
{
	if ( EqualValue != InValue )
	{
		Modify();
		EqualValue = InValue;
	}
}

void UDataprepIntegerFilter::SetFromValue(int InValue)
{
	if (FromValue != InValue)
	{
		Modify();
		FromValue = InValue;
	}
}

void UDataprepIntegerFilter::SetToValue(int InValue)
{
	if (ToValue != InValue)
	{
		Modify();
		ToValue = InValue;
	}
}
