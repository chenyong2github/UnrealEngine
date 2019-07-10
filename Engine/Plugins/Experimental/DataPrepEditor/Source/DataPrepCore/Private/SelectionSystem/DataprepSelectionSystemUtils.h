// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SelectionSystem/DataprepFetcher.h"
#include "SelectionSystem/DataprepFilter.h"

#include "Async/ParallelFor.h"
#include "CoreMinimal.h"
#include "Templates/UnrealTypeTraits.h"

namespace DataprepSelectionSystemUtils
{
	/**
	 * A implementation for the filtering of the objects using a filter and fetcher
	 * This implementation is already multi-threaded when it's possible.
	 */
	template< class FilterClass, class FetcherClass, class FetchedDataType>
	TArray<UObject*> FilterObjects(const FilterClass& Filter, const FetcherClass& Fetcher, const TArray<UObject*>& Objects)
	{
		static_assert( TIsDerivedFrom< FilterClass, UDataprepFilter >::IsDerived, "This implementation wasn't tested for a filter that isn't a child of DataprepFilter" );
		static_assert( TIsDerivedFrom< FetcherClass, UDataprepFetcher >::IsDerived, "This implementation wasn't tested for a fetcher that isn't a child of DataprepFetcher" );

		TArray<bool> FilterResultPerObject;
		FilterResultPerObject.AddZeroed( Objects.Num() );
		TAtomic<int32> ObjectThatPassedCount( 0 );

		auto UpdateFilterResult = [&Filter, &ObjectThatPassedCount, &FilterResultPerObject] (int32 Index, bool bFetchSucceded, const auto& FetchedData )
		{
			if (bFetchSucceded)
			{
				bool bPassedFilter = Filter.Filter( FetchedData );
				FilterResultPerObject[Index] = bPassedFilter;
				if ( bPassedFilter )
				{
					ObjectThatPassedCount++;
				}
			}
			else
			{
				FilterResultPerObject[Index] = false;
			}
		};

		if ( Filter.IsThreadSafe() && Fetcher.IsThreadSafe() )
		{
			ParallelFor( Objects.Num(), [&Fetcher, &Objects, &UpdateFilterResult](int32 Index)
			{
				bool bFetchSucceded = false;
				const FetchedDataType FetchedData = Fetcher.Fetch( Objects[Index], bFetchSucceded );
				UpdateFilterResult( Index, bFetchSucceded, FetchedData );
			});
		}

		else if ( Filter.IsThreadSafe() )
		{
			TArray< TPair< bool, FetchedDataType > > FetcherResults;
			FetcherResults.Reserve( Objects.Num() );
			for ( const UObject* Object : Objects )
			{
				bool bFetchSucceded = false;
				const FetchedDataType FetchedData = Fetcher.Fetch( Object, bFetchSucceded );
				FetcherResults.Emplace( bFetchSucceded, FetchedData );
			}

			ParallelFor( Objects.Num(), [&FetcherResults, &UpdateFilterResult](int32 Index)
			{
				const TPair< bool, FetchedDataType >& FetcherResult = FetcherResults[Index];
				UpdateFilterResult( Index, FetcherResult.Key, FetcherResult.Value );
			});
		}

		else if ( Fetcher.IsThreadSafe() )
		{
			TArray< TPair< bool, FetchedDataType > > FetcherResults;
			FetcherResults.AddZeroed( Objects.Num() );

			ParallelFor( Objects.Num(), [&Fetcher, &Objects, &FetcherResults](int32 Index)
			{
				bool bFetchSucceded = false;
				const FetchedDataType FetchedData = Fetcher.Fetch( Objects[Index], bFetchSucceded );
				FetcherResults[Index] = TPair< bool, FetchedDataType >( bFetchSucceded, FetchedData );
			});

			for (int32 Index = 0; Index < Objects.Num(); Index++)
			{
				const TPair< bool, FetchedDataType >& FetcherResult = FetcherResults[Index];
				UpdateFilterResult( Index, FetcherResult.Key, FetcherResult.Value );
			}
		}

		else
		{
			for (int32 Index = 0; Index < Objects.Num(); Index++)
			{
				bool bFetchSucceded = false;
				const FetchedDataType FetchedData = Fetcher.Fetch( Objects[Index], bFetchSucceded );
				UpdateFilterResult( Index, bFetchSucceded, FetchedData );
			}
		}

	
		const bool bIsExcludingObjectThatPassedFilter = Filter.IsExcludingResult();

		TArray< UObject* > SelectedObjects;
		int32 SelectionSize = bIsExcludingObjectThatPassedFilter ?  Objects.Num() - ObjectThatPassedCount.Load() : ObjectThatPassedCount.Load();
		SelectedObjects.Reserve( SelectionSize );
		for (int32 Index = 0; Index < Objects.Num(); Index++)
		{
			// If the filter is excluding the objects that passed the condition filter we inverse our selection criteria
			if ( FilterResultPerObject[Index] != bIsExcludingObjectThatPassedFilter )
			{
				SelectedObjects.Add( Objects[Index] );
			}
		}

		return SelectedObjects;
	}
}

