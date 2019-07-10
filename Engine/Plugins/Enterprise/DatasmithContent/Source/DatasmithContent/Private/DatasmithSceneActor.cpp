// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSceneActor.h"

#include "Engine/Engine.h"

ADatasmithSceneActor::ADatasmithSceneActor()
{
#if WITH_EDITOR
	if ( GEngine )
	{
		OnActorDeletedDelegateHandle = GEngine->OnLevelActorDeleted().AddUObject( this, &ADatasmithSceneActor::OnActorDeleted );
	}
#endif // WITH_EDITOR
}

ADatasmithSceneActor::~ADatasmithSceneActor()
{
#if WITH_EDITOR
	if ( GEngine )
	{
		GEngine->OnLevelActorDeleted().Remove( OnActorDeletedDelegateHandle );
	}
#endif // WITH_EDITOR
}


#if WITH_EDITOR
void ADatasmithSceneActor::OnActorDeleted(AActor* ActorDeleted)
{
	for ( TPair< FName, TSoftObjectPtr< AActor > >& Pair : RelatedActors )
	{
		AActor* RelatedActor = Pair.Value.Get();
		if ( RelatedActor == ActorDeleted )
		{
			// this will add this actor to the transaction if there is one currently recording
			Modify();

			Pair.Value.Reset();
		}
	}
}
#endif // WITH_EDITOR
