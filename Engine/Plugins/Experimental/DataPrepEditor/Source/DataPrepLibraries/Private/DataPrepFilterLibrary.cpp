// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepFilterLibrary.h"

#include "DatasmithContentBlueprintLibrary.h"
#include "Engine/StaticMesh.h"

namespace DataprepFilterLibraryImpl
{
	bool StringCompare( const FString& StringToCompare, const FString& SearchString, EEditorScriptingStringMatchType StringMatch )
	{
		switch ( StringMatch )
		{
			case EEditorScriptingStringMatchType::Contains:
				return StringToCompare.Contains( SearchString );

			case EEditorScriptingStringMatchType::ExactMatch:
				return StringToCompare.Equals( SearchString );

			case EEditorScriptingStringMatchType::MatchesWildcard:
				return StringToCompare.MatchesWildcard( SearchString );

			default:
				return false;
		}
	}

	template<typename T>
	T* CastIfValid(UObject* Target)
	{
		return Target->IsPendingKill() ? nullptr : Cast<T>(Target);
	}

}

TArray< UObject* > UDataprepFilterLibrary::FilterByClass(const TArray< UObject* >& TargetArray, TSubclassOf< UObject > ObjectClass )
{
	return UEditorFilterLibrary::ByClass( TargetArray, ObjectClass, EEditorScriptingFilterType::Include );
}

TArray< UObject* > UDataprepFilterLibrary::FilterByName( const TArray< UObject* >& TargetArray, const FString& NameSubString, EEditorScriptingStringMatchType StringMatch )
{
	return UEditorFilterLibrary::ByIDName( TargetArray, NameSubString, StringMatch, EEditorScriptingFilterType::Include );
}

TArray< UObject* > UDataprepFilterLibrary::FilterByMetadata( const TArray< UObject* >& TargetArray, FName Key, const FString& Value, EEditorScriptingStringMatchType ValueMatch )
{
	TArray< UObject* > Results;

	for ( UObject* Object : TargetArray )
	{
		FString KeyValue = UDatasmithContentBlueprintLibrary::GetDatasmithUserDataValueForKey( Object, Key );

		if ( !KeyValue.IsEmpty() && DataprepFilterLibraryImpl::StringCompare( KeyValue, Value, ValueMatch ) )
		{
			Results.Add( Object );
		}
	}

	return Results;
}

TArray< UObject* > UDataprepFilterLibrary::FilterBySize(const TArray< UObject* >& TargetArray, EDataprepSizeSource SizeSource, EDataprepSizeFilterMode FilterMode, float Threshold)
{
	TArray< UObject* > Result;

	auto ConditionnalAdd = [=, &Result](float RefValue, UObject* Object) -> void
	{
		if ( (RefValue <= Threshold && FilterMode == EDataprepSizeFilterMode::SmallerThan)
		  || (RefValue >= Threshold && FilterMode == EDataprepSizeFilterMode::BiggerThan) )
		{
			Result.Add(Object);
		}
	};

	switch (SizeSource)
	{
		case EDataprepSizeSource::BoundingBoxVolume:
		{
			auto GetAnyVolume = [](UObject* Object) -> FBox
			{
				if ( AActor* Actor = DataprepFilterLibraryImpl::CastIfValid<AActor>(Object) )
				{
					return Actor->GetComponentsBoundingBox();
				}
				else if ( UStaticMesh* Mesh = DataprepFilterLibraryImpl::CastIfValid<UStaticMesh>(Object) )
				{
					return Mesh->GetBoundingBox();
				}
				return FBox{};
			};

			for ( UObject* Object : TargetArray )
			{
				FBox BoundingVolume = GetAnyVolume(Object);
				if (BoundingVolume.IsValid)
				{
					ConditionnalAdd(BoundingVolume.GetVolume(), Object);
				}
			}

			break;
		}
	}

	return Result;
}

TArray< AActor* > UDataprepFilterLibrary::FilterByTag( const TArray< AActor* >& TargetArray, FName Tag )
{
	return UEditorFilterLibrary::ByActorTag( TargetArray, Tag, EEditorScriptingFilterType::Include );
}
