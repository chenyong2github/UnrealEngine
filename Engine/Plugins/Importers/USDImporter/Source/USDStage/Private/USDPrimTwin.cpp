// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimTwin.h"

#include "USDMemory.h"
#include "USDStageActor.h"

#include "Engine/World.h"

FUsdPrimTwin& FUsdPrimTwin::AddChild( const FString& InPrimPath )
{
	FScopedUnrealAllocs UnrealAllocs; // Make sure the call to new is done with the UE allocator

	FString Dummy;
	FString ChildPrimName;
	InPrimPath.Split( TEXT("/"), &Dummy, &ChildPrimName, ESearchCase::IgnoreCase, ESearchDir::FromEnd );

	TUniquePtr< FUsdPrimTwin >& ChildPrim = Children.Add( ChildPrimName );

	ChildPrim = MakeUnique< FUsdPrimTwin >();
	ChildPrim->PrimPath = InPrimPath;

	return *ChildPrim;
}

void FUsdPrimTwin::RemoveChild( const TCHAR* InPrimPath )
{
	FScopedUnrealAllocs UnrealAllocs;

	for ( TMap< FString, TUniquePtr< FUsdPrimTwin > >::TIterator ChildIt = Children.CreateIterator(); ChildIt; ++ChildIt )
	{
		if ( ChildIt->Value->PrimPath == InPrimPath )
		{
			ChildIt.RemoveCurrent();
			break;
		}
	}
}

void FUsdPrimTwin::Clear()
{
	FScopedUnrealAllocs UnrealAllocs;

	Children.Empty();

	if ( !PrimPath.IsEmpty() )
	{
		OnDestroyed.Broadcast( *this );
	}

	if ( SpawnedActor.IsValid() && !SpawnedActor->IsA<AUsdStageActor>() && !SpawnedActor->IsActorBeingDestroyed() )
	{
		SpawnedActor->GetWorld()->DestroyActor( SpawnedActor.Get() );
		SpawnedActor = nullptr;
	}
	else if ( SceneComponent.IsValid() && !SceneComponent->IsBeingDestroyed() )
	{
		SceneComponent->DestroyComponent();
		SceneComponent = nullptr;
	}
}

FUsdPrimTwin* FUsdPrimTwin::Find( const FString& InPrimPath )
{
	if ( PrimPath == InPrimPath )
	{
		return this;
	}

	FString RestOfPrimPathToFind;
	FString ChildPrimName;

	FString PrimPathToFind = InPrimPath;
	PrimPathToFind.RemoveFromStart( TEXT("/") );

	if ( !PrimPathToFind.Split( TEXT("/"), &ChildPrimName, &RestOfPrimPathToFind ) )
	{
		ChildPrimName = PrimPathToFind;
	}

	if ( ChildPrimName.IsEmpty() )
	{
		ChildPrimName = RestOfPrimPathToFind;
		RestOfPrimPathToFind.Empty();
	}

	if ( Children.Contains( ChildPrimName ) )
	{
		if ( RestOfPrimPathToFind.IsEmpty() )
		{
			return Children[ ChildPrimName ].Get();
		}
		else
		{
			return Children[ ChildPrimName ]->Find( RestOfPrimPathToFind );
		}
	}

	return nullptr;
}
