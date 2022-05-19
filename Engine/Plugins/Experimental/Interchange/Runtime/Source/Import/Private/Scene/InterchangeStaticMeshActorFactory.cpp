// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Scene/InterchangeStaticMeshActorFactory.h"

#include "InterchangeActorFactoryNode.h"
#include "InterchangeMeshActorFactoryNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Scene/InterchangeActorHelper.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"

UClass* UInterchangeStaticMeshActorFactory::GetFactoryClass() const
{
	return AStaticMeshActor::StaticClass();
}

UObject* UInterchangeStaticMeshActorFactory::CreateSceneObject(const UInterchangeFactoryBase::FCreateSceneObjectsParams& CreateSceneObjectsParams)
{
	AStaticMeshActor* SpawnedActor = Cast<AStaticMeshActor>(UE::Interchange::ActorHelper::SpawnFactoryActor(CreateSceneObjectsParams));

	if (!SpawnedActor)
	{
		return nullptr;
	}

	UInterchangeFactoryBaseNode* FactoryNode = CreateSceneObjectsParams.FactoryNode;
	SetupStaticMeshActor(CreateSceneObjectsParams.NodeContainer, FactoryNode, SpawnedActor);

	if (UStaticMeshComponent* StaticMeshComponent = SpawnedActor->GetStaticMeshComponent())
	{
		FactoryNode->ApplyAllCustomAttributeToObject(StaticMeshComponent);
	}

	return SpawnedActor;
};

void UInterchangeStaticMeshActorFactory::SetupStaticMeshActor(const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeFactoryBaseNode* ActorFactoryNode, AStaticMeshActor* StaticMeshActor)
{
	if (!StaticMeshActor)
	{
		return;
	}

	if (UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent())
	{
		StaticMeshComponent->UnregisterComponent();
	}
}

void UInterchangeStaticMeshActorFactory::PostImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
{
	// Set the static mesh on the component in the post import callback, once the static mesh has been fully imported.
	// The component doesn't like being set a static mesh with uninitialized render data.

	if (AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Arguments.ImportedObject))
	{
		if (UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent())
		{
			if (const UInterchangeFactoryBaseNode* MeshNode = UE::Interchange::ActorHelper::FindAssetInstanceFactoryNode(Arguments.NodeContainer, Arguments.FactoryNode))
			{
				if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshNode->ReferenceObject.TryLoad()))
				{
					StaticMeshComponent->SetStaticMesh(StaticMesh);

					if (const UInterchangeMeshActorFactoryNode* MeshActorFactoryNode = Cast<UInterchangeMeshActorFactoryNode>(Arguments.FactoryNode))
					{
						UE::Interchange::ActorHelper::ApplySlotMaterialDependencies(*Arguments.NodeContainer, *MeshActorFactoryNode, *StaticMeshComponent);
					}
				}
			}
		}
	}
}