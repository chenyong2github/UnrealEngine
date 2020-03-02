// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimTwin.h"

#include "USDMemory.h"
#include "USDStageActor.h"

#include "Engine/World.h"

UUsdPrimTwin& UUsdPrimTwin::AddChild( const FString& InPrimPath )
{
	FScopedUnrealAllocs UnrealAllocs; // Make sure the call to new is done with the UE allocator

	FString Dummy;
	FString ChildPrimName;
	InPrimPath.Split( TEXT("/"), &Dummy, &ChildPrimName, ESearchCase::IgnoreCase, ESearchDir::FromEnd );

	Modify();

	UUsdPrimTwin*& ChildPrim = Children.Add( ChildPrimName );

	ChildPrim = NewObject<UUsdPrimTwin>(this, NAME_None, RF_Transient | RF_Transactional);
	ChildPrim->PrimPath = InPrimPath;

	return *ChildPrim;
}

void UUsdPrimTwin::RemoveChild( const TCHAR* InPrimPath )
{
	FScopedUnrealAllocs UnrealAllocs;

	Modify();

	for ( TMap< FString, UUsdPrimTwin* >::TIterator ChildIt = Children.CreateIterator(); ChildIt; ++ChildIt )
	{
		if ( ChildIt->Value->PrimPath == InPrimPath )
		{
			ChildIt.RemoveCurrent();
			break;
		}
	}
}

void UUsdPrimTwin::Clear()
{
	FScopedUnrealAllocs UnrealAllocs;

	Modify();

	for (const TPair< FString, UUsdPrimTwin* >& Pair : Children)
	{
		Pair.Value->Clear();
	}
	Children.Empty();

	if ( !PrimPath.IsEmpty() )
	{
		OnDestroyed.Broadcast( *this );
	}

	AActor* ActorToDestroy = SpawnedActor.Get();

	if ( !ActorToDestroy )
	{
		if ( SceneComponent.IsValid() && SceneComponent->GetOwner()->GetRootComponent() == SceneComponent.Get() )
		{
			ActorToDestroy = SceneComponent->GetOwner();
		}
	}

	if ( ActorToDestroy && !ActorToDestroy->IsA< AUsdStageActor >() && !ActorToDestroy->IsActorBeingDestroyed() && ActorToDestroy->GetWorld() )
	{
		ActorToDestroy->Modify();
		ActorToDestroy->GetWorld()->DestroyActor( ActorToDestroy );
		SpawnedActor = nullptr;
	}
	else if ( SceneComponent.IsValid() && !SceneComponent->IsBeingDestroyed() )
	{
		SceneComponent->Modify();
		SceneComponent->DestroyComponent();
		SceneComponent = nullptr;
	}
}

UUsdPrimTwin* UUsdPrimTwin::Find( const FString& InPrimPath )
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
			return Children[ ChildPrimName ];
		}
		else
		{
			return Children[ ChildPrimName ]->Find( RestOfPrimPathToFind );
		}
	}

	return nullptr;
}
