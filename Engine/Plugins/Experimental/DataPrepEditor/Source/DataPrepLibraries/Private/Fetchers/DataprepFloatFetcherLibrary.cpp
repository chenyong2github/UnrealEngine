// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Fetchers/DataprepFloatFetcherLibrary.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"

/* UDataprepFloatVolumeFetcher methods
 *****************************************************************************/
float UDataprepFloatBoundingVolumeFetcher::Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const
{
	if ( Object && !Object->IsPendingKill() )
	{
		// build the static mesh if there isn't a render data (this a temporary work around)
		auto BuildStaticMeshIfNoRenderData = [](UStaticMesh* StaticMesh)
			{
				if ( !StaticMesh->RenderData || !StaticMesh->RenderData->IsInitialized() )
				{
					const_cast< UStaticMesh* >( StaticMesh )->Build( true );
				}
			};

		TOptional<float> Volume;
		if ( const AActor* Actor = Cast<const AActor>( Object ) )
		{
			FBox Box(ForceInit);

			for ( const UActorComponent* ActorComponent : Actor->GetComponents() )
			{
				if ( const UPrimitiveComponent* PrimComp = Cast<const UPrimitiveComponent>( ActorComponent ) )
				{
					if ( const UStaticMeshComponent* StaticMeshComponent = Cast<const UStaticMeshComponent>( ActorComponent ) )
					{
						BuildStaticMeshIfNoRenderData( StaticMeshComponent->GetStaticMesh() );
					}
					if ( PrimComp->IsRegistered() &&  PrimComp->IsCollisionEnabled() )
					{
						
						Box += PrimComp->Bounds.GetBox();
					}
				}
			}

			if ( Box.IsValid )
			{
				Volume = Box.GetVolume();
			}
		}
		else if ( const UStaticMesh* StaticMesh = Cast<const UStaticMesh>( Object ) )
		{
			BuildStaticMeshIfNoRenderData( const_cast<UStaticMesh*>( StaticMesh ) );
			Volume = StaticMesh->GetBoundingBox().GetVolume();
		}

		if ( Volume.IsSet() )
		{
			bOutFetchSucceded = true;
			return Volume.GetValue();
		}
	}

	bOutFetchSucceded = false;
	return {};
}

bool UDataprepFloatBoundingVolumeFetcher::IsThreadSafe() const
{
	// For 4.23 it will not be multi thread safe since mesh aren't always build
	return false;
}
