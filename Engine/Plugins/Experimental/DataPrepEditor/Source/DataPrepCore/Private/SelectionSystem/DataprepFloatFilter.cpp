// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SelectionSystem/DataprepFloatFilter.h"

#include "DataprepCoreLogCategory.h"
#include "SelectionSystem/DataprepSelectionSystemUtils.h"
#include "SelectionSystem/DataprepFloatFetcher.h"

bool UDataprepFloatFilter::Filter(float Float) const
{
	switch ( FloatMatchingCriteria )
	{
	case EDataprepFloatMatchType::LessThen:
		return Float < EqualValue;
		break;
	case EDataprepFloatMatchType::GreatherThen:
		return Float > EqualValue;
		break;
	case EDataprepFloatMatchType::IsNearlyEqual:
		return FMath::IsNearlyEqual( Float, EqualValue, Tolerance );
		break;
	default:
		check( false );
		break;
	}

	return false;
}

TArray<UObject*> UDataprepFloatFilter::FilterObjects(const TArray<UObject*>& Objects) const
{
	if ( FloatFetcher )
	{
		return DataprepSelectionSystemUtils::FilterObjects< UDataprepFloatFilter, UDataprepFloatFetcher, float >( *this, *FloatFetcher, Objects );
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepFloatFilter::FilterObjects: There was no Fetcher") );
	return {};
}

FText UDataprepFloatFilter::GetFilterCategoryText() const
{
	return NSLOCTEXT("DataprepFloatFilter", "FloatFilterCategory", "Float");
}

TSubclassOf<UDataprepFetcher> UDataprepFloatFilter::GetAcceptedFetcherClass() const
{
	return UDataprepFloatFetcher::StaticClass();
}

void UDataprepFloatFilter::SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass)
{
	UClass* NewFetcherClass = FetcherClass;
	if ( NewFetcherClass && NewFetcherClass->IsChildOf( GetAcceptedFetcherClass() ) )
	{
		UClass* OldFetcherClass = FloatFetcher ? FloatFetcher->GetClass() : nullptr;
		if ( NewFetcherClass != OldFetcherClass )
		{
			Modify();
			FloatFetcher = NewObject< UDataprepFloatFetcher >( this, NewFetcherClass, NAME_None, RF_Transactional );
		}
	}
	else
	{
		ensure( false );
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepFloatFilter::SetFetcher: The Fetcher Class is not compatible") );
	}
}

UDataprepFetcher* UDataprepFloatFilter::GetFetcher() const
{
	return FloatFetcher;
}

EDataprepFloatMatchType UDataprepFloatFilter::GetFloatMatchingCriteria() const
{
	return FloatMatchingCriteria;
}

float UDataprepFloatFilter::GetEqualValue() const
{
	return EqualValue;
}

float UDataprepFloatFilter::GetTolerance() const
{
	return Tolerance;
}

void UDataprepFloatFilter::SetFloatMatchingCriteria(EDataprepFloatMatchType InFloatMatchingCriteria)
{
	if ( FloatMatchingCriteria != InFloatMatchingCriteria )
	{
		Modify();
		FloatMatchingCriteria = InFloatMatchingCriteria;
	}
}

void UDataprepFloatFilter::SetEqualValue(float InEqualValue)
{
	if ( EqualValue != InEqualValue )
	{
		Modify();
		EqualValue = InEqualValue;
	}
}

void UDataprepFloatFilter::SetTolerance(float InTolerance)
{
	if ( Tolerance != InTolerance )
	{
		Modify();
		Tolerance = InTolerance;
	}
}
