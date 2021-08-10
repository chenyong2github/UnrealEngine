// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Scene/InterchangeActorFactory.h"

#include "InterchangeActorFactoryNode.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeSourceData.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			AActor* SpawnActor(const FActorSpawnParameters& ActorSpawnParameters, const UInterchangeActorFactoryNode* ActorFactoryNode, AActor* ParentActor)
			{
				ensure(ActorFactoryNode);

				if (!ActorFactoryNode)
				{
					return nullptr;
				}

				UWorld* const World = [&ActorSpawnParameters, &ParentActor]()
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

					return ActorSpawnParameters.OverrideLevel ? ActorSpawnParameters.OverrideLevel->GetWorld() : DefaultWorld;
				}();

				if (!World)
				{
					return nullptr;
				}

				FTransform Transform = FTransform::Identity;
				ActorFactoryNode->GetCustomGlobalTransform(Transform);

				AActor* SpawnedActor = World->SpawnActor<AActor>(ActorFactoryNode->GetObjectClass(), Transform, ActorSpawnParameters);

				if (SpawnedActor)
				{
#if WITH_EDITOR
					SpawnedActor->SetActorLabel(ActorSpawnParameters.Name.ToString());
#endif
					if (!SpawnedActor->GetRootComponent())
					{
						USceneComponent* RootComponent = NewObject<USceneComponent>(SpawnedActor, USceneComponent::GetDefaultSceneRootVariableName(), RF_Transactional);
						RootComponent->Mobility = EComponentMobility::Static; //todo: this should be an attribute on a factory node
#if WITH_EDITORONLY_DATA
						RootComponent->bVisualizeComponent = true;
#endif
						RootComponent->SetWorldTransform(Transform);

						SpawnedActor->SetRootComponent(RootComponent);
						SpawnedActor->AddInstanceComponent(RootComponent);

						RootComponent->RegisterComponent();
					}
				}

				if (ParentActor)
				{
					SpawnedActor->AttachToActor(ParentActor, FAttachmentTransformRules::KeepWorldTransform);
				}

				return SpawnedActor;
			}

			void RecursivelySpawnActors(TMap<FString, UObject*>& OutSpawnedActors,
				const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeActorFactoryNode* ActorNode, const FActorSpawnParameters& SpawnParameters,
				AActor* ParentActor, const bool bSpawnChildren)
			{
				if (!ActorNode || !NodeContainer)
				{
					return;
				}

				const FString ActorNodeUniqueID = ActorNode->GetUniqueID();

				AActor* SpawnedActor = SpawnActor(SpawnParameters, Cast<UInterchangeActorFactoryNode>(ActorNode), ParentActor);
				OutSpawnedActors.Add(ActorNodeUniqueID, SpawnedActor);

				if (bSpawnChildren)
				{
					for (int32 ChildIndex = 0; ChildIndex < NodeContainer->GetNodeChildrenCount(ActorNodeUniqueID); ++ChildIndex)
					{
						if (const UInterchangeBaseNode* ChildNode = NodeContainer->GetNodeChildren(ActorNodeUniqueID, ChildIndex))
						{
							if (const UInterchangeActorFactoryNode* ChildActorFactoryNode = Cast<const UInterchangeActorFactoryNode>(ChildNode))
							{
								FActorSpawnParameters ChildActorSpawnParameters;
								ChildActorSpawnParameters.Name = FName(ChildActorFactoryNode->GetDisplayLabel());
								ChildActorSpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
								ChildActorSpawnParameters.OverrideLevel = SpawnParameters.OverrideLevel;
								ChildActorSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

								RecursivelySpawnActors(OutSpawnedActors, NodeContainer, ChildActorFactoryNode, ChildActorSpawnParameters, SpawnedActor, bSpawnChildren);
							}
						}
					}
				}
			};
		}
	}
}

UClass* UInterchangeActorFactory::GetFactoryClass() const
{
	return AActor::StaticClass();
}

TMap<FString, UObject*> UInterchangeActorFactory::CreateSceneObjects(const UInterchangeFactoryBase::FCreateSceneObjectsParams& CreateSceneObjectsParams)
{
	FActorSpawnParameters ActorSpawnParameters;
	ActorSpawnParameters.Name = FName(*CreateSceneObjectsParams.ObjectName);
	ActorSpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	ActorSpawnParameters.OverrideLevel = CreateSceneObjectsParams.Level;
	ActorSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	TMap<FString, UObject*> SpawnedActors;
	constexpr AActor* ParentActor = nullptr;

	UE::Interchange::Private::RecursivelySpawnActors(SpawnedActors, CreateSceneObjectsParams.NodeContainer, Cast<const UInterchangeActorFactoryNode>(CreateSceneObjectsParams.ObjectNode),
		ActorSpawnParameters, ParentActor, CreateSceneObjectsParams.bCreateSceneObjectsForChildren);

	return SpawnedActors;
}