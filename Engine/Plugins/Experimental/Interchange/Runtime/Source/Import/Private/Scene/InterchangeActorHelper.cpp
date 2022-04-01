// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Scene/InterchangeActorHelper.h"

#include "InterchangeActorFactoryNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#include "Engine/World.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif

AActor* UE::Interchange::ActorHelper::GetSpawnedParentActor(const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeActorFactoryNode* FactoryNode)
{
	AActor* ParentActor = nullptr;

	if (const UInterchangeFactoryBaseNode* ParentFactoryNode = Cast<UInterchangeFactoryBaseNode>(NodeContainer->GetNode(FactoryNode->GetParentUid())))
	{
		ParentActor = Cast<AActor>(ParentFactoryNode->ReferenceObject.TryLoad());
	}

	return  ParentActor;
}

AActor* UE::Interchange::ActorHelper::SpawnFactoryActor(const UInterchangeFactoryBase::FCreateSceneObjectsParams& CreateSceneObjectsParams)
{
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = FName(*CreateSceneObjectsParams.ObjectName);
	SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	SpawnParameters.OverrideLevel = CreateSceneObjectsParams.Level;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const UInterchangeActorFactoryNode* FactoryNode = Cast<UInterchangeActorFactoryNode>(CreateSceneObjectsParams.FactoryNode);
	const UInterchangeBaseNodeContainer* NodeContainer = CreateSceneObjectsParams.NodeContainer;

	if (!FactoryNode || !NodeContainer)
	{
		return nullptr;
	}

	AActor* ParentActor = UE::Interchange::ActorHelper::GetSpawnedParentActor(NodeContainer, FactoryNode);
	UWorld* const World = [&SpawnParameters, &ParentActor]()
	{
		UWorld* DefaultWorld = nullptr;

		if (ParentActor)
		{
			DefaultWorld = ParentActor->GetWorld();
		}
#if WITH_EDITOR
		if (DefaultWorld == nullptr)
		{
			UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
			if (GIsEditor && EditorEngine != nullptr)
			{
				DefaultWorld = EditorEngine->GetEditorWorldContext().World();
			}
		}
#endif
		if (DefaultWorld == nullptr && GEngine)
		{
			DefaultWorld = GEngine->GetWorld();
		}

		return SpawnParameters.OverrideLevel ? SpawnParameters.OverrideLevel->GetWorld() : DefaultWorld;
	}();

	if (!World)
	{
		return nullptr;
	}

	FTransform Transform = FTransform::Identity;
	FactoryNode->GetCustomGlobalTransform(Transform);

	UClass* ActorClass = FactoryNode->GetObjectClass();
	AActor* SpawnedActor = World->SpawnActor<AActor>(ActorClass, Transform, SpawnParameters);

	if (SpawnedActor)
	{
#if WITH_EDITOR
		SpawnedActor->SetActorLabel(SpawnParameters.Name.ToString());
#endif
		if (!SpawnedActor->GetRootComponent())
		{
			USceneComponent* RootComponent = NewObject<USceneComponent>(SpawnedActor, USceneComponent::GetDefaultSceneRootVariableName(), RF_Transactional);
#if WITH_EDITORONLY_DATA
			RootComponent->bVisualizeComponent = true;
#endif
			RootComponent->SetWorldTransform(Transform);

			SpawnedActor->SetRootComponent(RootComponent);
			SpawnedActor->AddInstanceComponent(RootComponent);
		}

		if (USceneComponent* RootComponent = SpawnedActor->GetRootComponent())
		{
			uint8 Mobility;
			if (FactoryNode->GetCustomMobility(Mobility))
			{
				//Make sure we don't have a mobility that's more restrictive than our parent mobility, as that wouldn't be a valid setup.
				EComponentMobility::Type TargetMobility = (EComponentMobility::Type)Mobility;

				if (ParentActor && ParentActor->GetRootComponent())
				{
					TargetMobility = (EComponentMobility::Type)FMath::Max((uint8)Mobility, (uint8)ParentActor->GetRootComponent()->Mobility);
				}

				RootComponent->SetMobility(TargetMobility);
			}
		}
	}

	if (ParentActor)
	{
		SpawnedActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform);
	}

	return SpawnedActor;
}

const UInterchangeBaseNode* UE::Interchange::ActorHelper::FindAssetInstanceFactoryNode(const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeBaseNode* ActorFactoryNode)
{
	TArray<FString> ActorTargetNodes;
	ActorFactoryNode->GetTargetNodeUids(ActorTargetNodes);
	const UInterchangeSceneNode* SceneNode = ActorTargetNodes.IsEmpty() ? nullptr : Cast<UInterchangeSceneNode>(NodeContainer->GetNode(ActorTargetNodes[0]));
	if (!SceneNode)
	{
		return nullptr;
	}

	FString AssetInstanceUid;
	SceneNode->GetCustomAssetInstanceUid(AssetInstanceUid);
	const UInterchangeBaseNode* AssetNode = NodeContainer->GetNode(AssetInstanceUid);
	if (!AssetNode)
	{
		return nullptr;
	}

	TArray<FString> AssetTargetNodeIds;
	AssetNode->GetTargetNodeUids(AssetTargetNodeIds);
	return AssetTargetNodeIds.IsEmpty() ? nullptr : NodeContainer->GetNode(AssetTargetNodeIds[0]);
}