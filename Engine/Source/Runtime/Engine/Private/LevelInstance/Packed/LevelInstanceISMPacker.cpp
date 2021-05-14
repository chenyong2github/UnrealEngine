// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/Packed/LevelInstanceISMPacker.h"

#if WITH_EDITOR

#include "LevelInstance/Packed/PackedLevelInstanceBuilder.h"
#include "LevelInstance/Packed/PackedLevelInstanceActor.h"

#include "Templates/TypeHash.h"

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
		if (InContext.ShouldPackComponent(StaticMeshComponent))
		{
			FLevelInstancePackerClusterID ClusterID(MakeUnique<FLevelInstanceISMPackerCluster>(GetID(), StaticMeshComponent));

			InContext.FindOrAddCluster(MoveTemp(ClusterID), StaticMeshComponent);
		}
	}
}

void FLevelInstanceISMPacker::PackActors(FPackedLevelInstanceBuilderContext& InContext, APackedLevelInstance* InPackingActor, const FLevelInstancePackerClusterID& InClusterID, const TArray<UActorComponent*>& InComponents) const
{
	check(InClusterID.GetPackerID() == GetID());
	UInstancedStaticMeshComponent* PackComponent = InPackingActor->AddPackedComponent<UInstancedStaticMeshComponent>();
	
	FTransform ActorTransform = InPackingActor->GetActorTransform();
	FTransform CurrentPivotOffsetInverse = ActorTransform.GetRelativeTransform(InContext.GetLevelTransform());

	PackComponent->SetComponentToWorld(ActorTransform);
	PackComponent->AttachToComponent(InPackingActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	
	FLevelInstanceISMPackerCluster* ISMCluster = (FLevelInstanceISMPackerCluster*)InClusterID.GetData();
	check(ISMCluster);

	ISMCluster->ISMDescriptor.InitComponent(PackComponent);

	TArray<FTransform> InstanceTransforms;
	for (UActorComponent* Component : InComponents)
	{
		// If we have a ISM we need to add all instances
		if (UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(Component))
		{
			for (int32 InstanceIndex = 0; InstanceIndex < ISMComponent->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;

				if (ensure(ISMComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, /*bWorldSpace=*/ true)))
				{
					InstanceTransforms.Add(InstanceTransform);
				}
			}
		}
		else // other subclasses are processed like regular UStaticMeshComponent
		{
			UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(Component);
			InstanceTransforms.Add(StaticMeshComponent->GetComponentTransform());
		}
	}
	PackComponent->AddInstances(InstanceTransforms, /*bWorldSpace*/true);

	FTransform NewWorldTransform = ActorTransform * CurrentPivotOffsetInverse * FTransform(InContext.GetPivotOffset());

	PackComponent->SetWorldTransform(NewWorldTransform);
	PackComponent->RegisterComponent();
}

FLevelInstanceISMPackerCluster::FLevelInstanceISMPackerCluster(FLevelInstancePackerID InPackerID, UStaticMeshComponent* InComponent)
	: FLevelInstancePackerCluster(InPackerID)
{
	ISMDescriptor.InitFrom(InComponent, /** bInitBodyInstance= */ false);
	// Component descriptor should be considered hidden if original actor owner was.
	ISMDescriptor.bHiddenInGame |= InComponent->GetOwner()->IsHidden();
	ISMDescriptor.BodyInstance.CopyRuntimeBodyInstancePropertiesFrom(&InComponent->BodyInstance);
	ISMDescriptor.ComputeHash();
}

uint32 FLevelInstanceISMPackerCluster::ComputeHash() const
{
	return HashCombine(FLevelInstancePackerCluster::ComputeHash(), ISMDescriptor.Hash);
}

bool FLevelInstanceISMPackerCluster::operator==(const FLevelInstancePackerCluster& InOther) const
{
	const FLevelInstanceISMPackerCluster& ISMOther = (const FLevelInstanceISMPackerCluster&)InOther;
	return FLevelInstancePackerCluster::operator==(InOther) && ISMDescriptor == ISMOther.ISMDescriptor;
}

#endif