// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Fetchers/DataprepFloatFetcherLibrary.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "MeshAttributeArray.h"
#include "MeshAttributes.h"
#include "MeshDescription.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"

/* UDataprepFloatVolumeFetcher methods
 *****************************************************************************/
float UDataprepFloatBoundingVolumeFetcher::Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const
{
	if ( Object && !Object->IsPendingKill() )
	{
		auto GetStaticMeshBoundingBox = [](const UStaticMesh* StaticMesh) -> FBox
			{
				if ( !StaticMesh->RenderData || !StaticMesh->RenderData->IsInitialized() )
				{
					FBox Box(ForceInit);
					if ( const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription( 0 ) )
					{
						TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>( MeshAttribute::Vertex::Position );
						for ( const FVertexID VertexID : MeshDescription->Vertices().GetElementIDs() )
						{
							if ( !MeshDescription->IsVertexOrphaned( VertexID ) )
							{
								Box += VertexPositions[VertexID];
							}
						}
					}
					return Box;
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
					if ( PrimComp->IsRegistered() &&  PrimComp->IsCollisionEnabled() )
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
