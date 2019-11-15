// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Fetchers/DataprepFloatFetcherLibrary.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"

#define LOCTEXT_NAMESPACE "DataprepFloatFetcherLibrary"

/* UDataprepFloatVolumeFetcher methods+
 *****************************************************************************/
float UDataprepFloatBoundingVolumeFetcher::Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const
{
	if ( Object && !Object->IsPendingKill() )
	{
		auto GetStaticMeshBoundingBox = [](const UStaticMesh* StaticMesh) -> FBox
			{
				if ( !StaticMesh )
				{
					return {};
				}

				return StaticMesh->GetBoundingBox();
			};

		TOptional<float> Volume;
		if ( const AActor* Actor = Cast<const AActor>( Object ) )
		{
			FBox ActorBox(ForceInit);

			for ( const UActorComponent* ActorComponent : Actor->GetComponents() )
			{
				if ( const UPrimitiveComponent* PrimComp = Cast<const UPrimitiveComponent>( ActorComponent ) )
				{
					FBox ComponentBox(ForceInit);
					if ( const UStaticMeshComponent* StaticMeshComponent = Cast<const UStaticMeshComponent>( ActorComponent ) )
					{
						ComponentBox = GetStaticMeshBoundingBox( StaticMeshComponent->GetStaticMesh() );
						ComponentBox.TransformBy( PrimComp->GetComponentToWorld() );
					}
					if ( PrimComp->IsRegistered() )
					{
						ActorBox += ComponentBox.IsValid? ComponentBox : PrimComp->Bounds.GetBox();
					}
				}
			}

			if ( ActorBox.IsValid )
			{
				Volume = ActorBox.GetVolume();
			}
		}
		else if ( const UStaticMesh* StaticMesh = Cast<const UStaticMesh>( Object ) )
		{
			Volume = GetStaticMeshBoundingBox( StaticMesh ).GetVolume();
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
	return true;
}

FText UDataprepFloatBoundingVolumeFetcher::GetNodeDisplayFetcherName_Implementation() const
{
	return LOCTEXT("BoundingVolumeFilterTitle", "Bounding Volume");
}

#undef LOCTEXT_NAMESPACE
