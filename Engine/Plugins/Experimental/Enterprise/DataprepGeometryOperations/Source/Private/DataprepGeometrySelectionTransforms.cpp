// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepGeometrySelectionTransforms.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "IMeshMergeUtilities.h"
#include "DataprepOperationsLibraryUtil.h"
#include "DataprepGeometryOperations.h"
#include "Materials/MaterialInstance.h"
#include "MeshDescriptionAdapter.h"
#include "MeshMergeModule.h"
#include "MeshAttributes.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshOperations.h"

#define LOCTEXT_NAMESPACE "DataprepGeometrySelectionTransforms"

namespace DataprepGeometryOperationsUtils
{
	void FindOverlappingActors( const TArray<AActor*>& InActorsToTest, const TArray<AActor*>& InActorsToTestAgainst, TArray<AActor*>& OutOverlappingActors, bool bSilent )
	{
		if( InActorsToTestAgainst.Num() == 0 || InActorsToTest.Num() == 0 )
		{
			UE_LOG( LogDataprepGeometryOperations, Warning, TEXT("FindOverlappingActors: No actors to process. Aborting...") );
			return;
		}

		TFunction<void(const TArray<AActor*>&, TArray<UStaticMeshComponent*>&)> GetActorsComponents = []( const TArray<AActor*>& InActors, TArray<UStaticMeshComponent*>& OutComponents )
		{
			for( AActor* Actor : InActors )
			{
				if( Actor == nullptr )
				{
					continue;
				}

				for( UActorComponent* Component : Actor->GetComponents() )
				{
					if( UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component) )
					{
						if( StaticMeshComponent->GetStaticMesh() == nullptr )
						{
							continue;
						}
						OutComponents.Add( StaticMeshComponent );
					}
				}
			}
		};

		TArray<UStaticMeshComponent*> ComponentsToMerge;

		GetActorsComponents(InActorsToTestAgainst, ComponentsToMerge);

		if( ComponentsToMerge.Num() == 0 )
		{
			UE_LOG(LogDataprepGeometryOperations, Warning, TEXT("FindOverlappingActors: No meshes to process. Aborting..."));
			return;
		}

		TSet<UStaticMesh*> StaticMeshes;
		TArray<UPrimitiveComponent*> PrimitiveComponentsToMerge; // because of MergeComponentsToStaticMesh
		for(UStaticMeshComponent* StaticMeshComponent : ComponentsToMerge )
		{
			PrimitiveComponentsToMerge.Add(StaticMeshComponent);

			if( StaticMeshComponent->GetStaticMesh()->GetRenderData() == nullptr )
			{
				StaticMeshes.Add( StaticMeshComponent->GetStaticMesh() );
			}
		}

		DataprepOperationsLibraryUtil::FStaticMeshBuilder StaticMeshBuilder( StaticMeshes );

		const IMeshMergeUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>( "MeshMergeUtilities" ).GetUtilities();

		FMeshMergingSettings MergeSettings;
		FVector MergedMeshWorldLocation;
		TArray<UObject*> CreatedAssets;
		const float ScreenAreaSize = TNumericLimits<float>::Max();
		
		MeshUtilities.MergeComponentsToStaticMesh( PrimitiveComponentsToMerge, nullptr, MergeSettings, nullptr, GetTransientPackage(), FString(), CreatedAssets, MergedMeshWorldLocation, ScreenAreaSize, true );

		UStaticMesh* MergedMesh = nullptr;
		if( !CreatedAssets.FindItemByClass( &MergedMesh ) )
		{
			UE_LOG(LogDataprepGeometryOperations, Error, TEXT("MergeStaticMeshActors failed. No mesh was created."));
			return;
		}

		// Transform raw mesh vertex data by the Static Mesh Component's component to world transformation
		FStaticMeshOperations::ApplyTransform( *MergedMesh->GetMeshDescription(0), FTransform(MergedMeshWorldLocation) );

		// Buil mesh tree to test intersections
		FMeshDescriptionTriangleMeshAdapter MergedMeshAdapter( MergedMesh->GetMeshDescription(0) );
		UE::Geometry::TMeshAABBTree3 <FMeshDescriptionTriangleMeshAdapter> MergedMeshTree(&MergedMeshAdapter);

		MergedMeshTree.Build();

		check( MergedMeshTree.IsValid() );

		using FAxisAlignedBox3d = UE::Geometry::FAxisAlignedBox3d;
		const FAxisAlignedBox3d MergedMeshBox = MergedMeshTree.GetBoundingBox();

		TSet< AActor* > OverlappingActorSet;
		OverlappingActorSet.Reserve( ComponentsToMerge.Num() );

		TArray<UStaticMeshComponent*> StaticMeshComponentsToTest;
		GetActorsComponents( InActorsToTest, StaticMeshComponentsToTest );

		// Check each actor agains volume
		for( UStaticMeshComponent* StaticMeshComponent : StaticMeshComponentsToTest )
		{
			const FAxisAlignedBox3d MeshBox( StaticMeshComponent->Bounds.GetBox() );
			bool bOverlap = MeshBox.Intersects( MergedMeshBox );

			if( bOverlap )
			{
				// Component's bounding box intersects with volume, check on vertices
				bOverlap = false;

				if( const UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh() )
				{
					const FMeshDescriptionTriangleMeshAdapter MeshAdapter( Mesh->GetMeshDescription(0) );
					const FTransform& ComponentTransform = StaticMeshComponent->GetComponentTransform();

					bOverlap = MergedMeshTree.TestIntersection( &MeshAdapter, FAxisAlignedBox3d::Empty(), [&ComponentTransform]( const FVector3d& InVert ) -> FVector3d
					{
						return ComponentTransform.TransformPosition( FVector( InVert.X, InVert.Y, InVert.Z ) );
					});

					if( bOverlap )
					{
						AActor* Actor = StaticMeshComponent->GetOwner();
						OverlappingActorSet.Add( Actor );
					}
				}
			}
		}

		OutOverlappingActors = OverlappingActorSet.Array();
	}
}

void UDataprepOverlappingActorsSelectionTransform::OnExecution_Implementation(const TArray<UObject*>& InObjects, TArray<UObject*>& OutObjects)
{
	TSet<AActor*> TargetActors;
	UWorld* World = nullptr;

	for (UObject* Object : InObjects)
	{
		if (!ensure(Object) || Object->IsPendingKill())
		{
			continue;
		}

		if (AActor* Actor = Cast< AActor >(Object))
		{
			if (World == nullptr)
			{
				World = Actor->GetWorld();
			}
			TargetActors.Add(Actor);
		}
	}

	if (World == nullptr || TargetActors.Num() == 0)
	{
		return;
	}

	TArray<AActor*> WorldActors;

	// Get all world actors that we want to test against our input set.
	for (ULevel* Level : World->GetLevels())
	{
		for (AActor* Actor : Level->Actors)
		{
			if (Actor)
			{
				// Skip actors that are present in the input.
				if (TargetActors.Contains(Actor) || Actor->IsPendingKillOrUnreachable())
				{
					continue;
				}
				WorldActors.Add(Actor);
			}
		}
	}

	// Run the overlap test.
	TArray<AActor*> OverlappingActors;

	DataprepGeometryOperationsUtils::FindOverlappingActors(WorldActors, TargetActors.Array(), OverlappingActors, true);

	OutObjects.Append(OverlappingActors);
}

#undef LOCTEXT_NAMESPACE
