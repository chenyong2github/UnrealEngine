// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Scene/InterchangeSkeletalMeshActorFactory.h"

#include "InterchangeActorFactoryNode.h"
#include "Scene/InterchangeActorHelper.h"

#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"

#include "Nodes/InterchangeBaseNodeContainer.h"


UObject* UInterchangeSkeletalMeshActorFactory::CreateSceneObject(const UInterchangeFactoryBase::FCreateSceneObjectsParams& CreateSceneObjectsParams)
{
	ASkeletalMeshActor* SpawnedActor = Cast<ASkeletalMeshActor>(UE::Interchange::ActorHelper::SpawnFactoryActor(CreateSceneObjectsParams));

	if (!SpawnedActor)
	{
		return nullptr;
	}

	UInterchangeFactoryBaseNode* FactoryNode = CreateSceneObjectsParams.FactoryNode;
	SetupSkeletalMeshActor(CreateSceneObjectsParams.NodeContainer, FactoryNode, SpawnedActor);

	if (USkeletalMeshComponent* SkeletalMeshComponent = SpawnedActor->GetSkeletalMeshComponent())
	{
		FactoryNode->ApplyAllCustomAttributeToObject(SkeletalMeshComponent);
	}

	return SpawnedActor;
};

UClass* UInterchangeSkeletalMeshActorFactory::GetFactoryClass() const
{
	return ASkeletalMeshActor::StaticClass();
}

void UInterchangeSkeletalMeshActorFactory::SetupSkeletalMeshActor(const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeFactoryBaseNode* ActorFactoryNode, ASkeletalMeshActor* SkeletalMeshActor)
{
	USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshActor->GetSkeletalMeshComponent();
	SkeletalMeshComponent->UnregisterComponent();

	if (const UInterchangeFactoryBaseNode* MeshNode = UE::Interchange::ActorHelper::FindAssetInstanceFactoryNode(NodeContainer, ActorFactoryNode))
	{
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshNode->ReferenceObject.TryLoad()))
		{
			SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
		}
	}
}
