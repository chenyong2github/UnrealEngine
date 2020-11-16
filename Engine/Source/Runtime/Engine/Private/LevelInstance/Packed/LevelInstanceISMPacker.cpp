// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/Packed/LevelInstanceISMPacker.h"

#if WITH_EDITOR

#include "LevelInstance/Packed/PackedLevelInstanceBuilder.h"
#include "LevelInstance/Packed/PackedLevelInstanceActor.h"

#include "Misc/Crc.h"

#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"

FLevelInstancePackerID FLevelInstanceISMPacker::PackerID = 'ISMP';

FLevelInstancePackerID FLevelInstanceISMPacker::GetID() const
{
	return PackerID;
}

void FLevelInstanceISMPacker::GetPackClusters(FPackedLevelInstanceBuilderContext& InContext, AActor* InActor) const
{
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	InActor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);

	for(UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		FLevelInstancePackerClusterID ClusterID(MakeUnique<FLevelInstanceISMPackerCluster>(GetID(), StaticMeshComponent));

		InContext.FindOrAddCluster(MoveTemp(ClusterID), StaticMeshComponent);
	}
}

void FLevelInstanceISMPacker::PackActors(FPackedLevelInstanceBuilderContext& InContext, APackedLevelInstance* InPackingActor, const FLevelInstancePackerClusterID& InClusterID, const TArray<UActorComponent*>& InComponents) const
{
	check(InClusterID.GetPackerID() == GetID());
	UInstancedStaticMeshComponent* PackComponent = InPackingActor->AddPackedComponent<UInstancedStaticMeshComponent>();
	
	PackComponent->SetComponentToWorld(InPackingActor->GetActorTransform());
	PackComponent->AttachToComponent(InPackingActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	
	FLevelInstanceISMPackerCluster* ISMCluster = (FLevelInstanceISMPackerCluster*)InClusterID.GetData();
	check(ISMCluster);

	PackComponent->bReceivesDecals = ISMCluster->bReceivesDecals;
	PackComponent->CastShadow = ISMCluster->bCastShadow;
	PackComponent->bVisibleInRayTracing = ISMCluster->bVisibleInRayTracing;
	PackComponent->SetStaticMesh(ISMCluster->StaticMesh);
	for (int32 MaterialIndex = 0; MaterialIndex < ISMCluster->Materials.Num(); ++MaterialIndex)
	{
		PackComponent->SetMaterial(MaterialIndex, ISMCluster->Materials[MaterialIndex]);
	}

	for (UActorComponent* Component : InComponents)
	{
		// If we have a ISM we need to add all instances
		if (UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(Component))
		{
			for (int32 InstanceIndex = 0; InstanceIndex < ISMComponent->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;

				if (ensure(ISMComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, true)))
				{
					PackComponent->AddInstanceWorldSpace(InstanceTransform);
				}
			}
		}
		else // other subclasses are processed like regular UStaticMeshComponent
		{
			UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(Component);
			PackComponent->AddInstanceWorldSpace(StaticMeshComponent->GetComponentTransform());
		}
	}
	PackComponent->RegisterComponent();
}

FLevelInstanceISMPackerCluster::FLevelInstanceISMPackerCluster(FLevelInstancePackerID InPackerID, UStaticMeshComponent* InComponent)
	: FLevelInstancePackerCluster(InPackerID)
	, StaticMesh(InComponent->GetStaticMesh())
	, Materials(InComponent->GetMaterials())
	, bReceivesDecals(InComponent->bReceivesDecals)
	, bCastShadow(InComponent->CastShadow)
	, bVisibleInRayTracing(InComponent->bVisibleInRayTracing)
{
	
}

uint32 FLevelInstanceISMPackerCluster::ComputeHash() const
{
	uint32 ParentHash = FLevelInstancePackerCluster::ComputeHash();
	// Hash pointers for now since we are not persisting the clusters
	uint32 Hash = FCrc::TypeCrc32(StaticMesh, ParentHash);
	for (UMaterialInterface* Material : Materials)
	{
		Hash = FCrc::TypeCrc32(Material, Hash);
	}
	Hash = FCrc::TypeCrc32(bReceivesDecals, Hash);
	Hash = FCrc::TypeCrc32(bCastShadow, Hash);
	Hash = FCrc::TypeCrc32(bVisibleInRayTracing, Hash);

	return Hash;
}

bool FLevelInstanceISMPackerCluster::operator==(const FLevelInstancePackerCluster& InOther) const
{
	const FLevelInstanceISMPackerCluster& ISMOther = (const FLevelInstanceISMPackerCluster&)InOther;
	return FLevelInstancePackerCluster::operator==(InOther) &&
		StaticMesh == ISMOther.StaticMesh &&
		Materials == ISMOther.Materials &&
		bReceivesDecals == ISMOther.bReceivesDecals &&
		bCastShadow == ISMOther.bCastShadow &&
		bVisibleInRayTracing == ISMOther.bVisibleInRayTracing;
}

#endif