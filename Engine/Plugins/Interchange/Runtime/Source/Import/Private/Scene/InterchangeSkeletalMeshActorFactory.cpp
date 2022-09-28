// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Scene/InterchangeSkeletalMeshActorFactory.h"

#include "InterchangeActorFactoryNode.h"
#include "InterchangeMeshActorFactoryNode.h"
#include "Scene/InterchangeActorHelper.h"

#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"

#include "Nodes/InterchangeBaseNodeContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeSkeletalMeshActorFactory)


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
}

void UInterchangeSkeletalMeshActorFactory::PostImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
{
	// Set the skeletal mesh on the component in the post import callback, once the skeletal mesh has been fully imported.

	if (ASkeletalMeshActor* SkeletalMeshActor = Cast<ASkeletalMeshActor>(Arguments.ImportedObject))
	{
		if (USkeletalMeshComponent * SkeletalMeshComponent = SkeletalMeshActor->GetSkeletalMeshComponent())
		{
			if (const UInterchangeFactoryBaseNode* MeshNode = UE::Interchange::ActorHelper::FindAssetInstanceFactoryNode(Arguments.NodeContainer, Arguments.FactoryNode))
			{
				FSoftObjectPath ReferenceObject;
				MeshNode->GetCustomReferenceObject(ReferenceObject);
				if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(ReferenceObject.TryLoad()))
				{
					SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);

					if (const UInterchangeMeshActorFactoryNode* MeshActorFactoryNode = Cast<UInterchangeMeshActorFactoryNode>(Arguments.FactoryNode))
					{
						UE::Interchange::ActorHelper::ApplySlotMaterialDependencies(*Arguments.NodeContainer, *MeshActorFactoryNode, *SkeletalMeshComponent);
					}
				}
			}
		}
	}
}
