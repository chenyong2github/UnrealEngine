// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeGenericScenesPipeline.h"

#include "InterchangeActorFactoryNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangeSceneNode.h"

bool UInterchangeGenericLevelPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericAssetsPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return false;
	}

	TArray<UInterchangeSceneNode*> SceneNodes;

	//Find all translated node we need for this pipeline
	InBaseNodeContainer->IterateNodes([&SceneNodes](const FString& NodeUid, UInterchangeBaseNode* Node)
	{
		switch(Node->GetnodeContainerType())
		{
		case EInterchangeNodeContainerType::NodeContainerType_TranslatedScene:
		{
			if (UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(Node))
			{
				SceneNodes.Add(SceneNode);
			}
		}
		break;
		}
	});

	for (const UInterchangeSceneNode* SceneNode : SceneNodes)
	{
		CreateActorFactoryNode(SceneNode, InBaseNodeContainer);
	}

	return true;
}

void UInterchangeGenericLevelPipeline::CreateActorFactoryNode(const UInterchangeSceneNode* SceneNode, UInterchangeBaseNodeContainer* FactoryNodeContainer)
{
	if (!SceneNode)
	{
		return;
	}

	UInterchangeActorFactoryNode* ActorFactoryNode = NewObject<UInterchangeActorFactoryNode>(FactoryNodeContainer, NAME_None);

	if (!ensure(ActorFactoryNode))
	{
		return;
	}

	ActorFactoryNode->InitializeNode(TEXT("Factory_") + SceneNode->GetUniqueID(), SceneNode->GetDisplayLabel(), EInterchangeNodeContainerType::NodeContainerType_FactoryData);

	if (!SceneNode->GetParentUid().IsEmpty())
	{
		ActorFactoryNode->SetParentUid(TEXT("Factory_") + SceneNode->GetParentUid());
	}

	FTransform GlobalTransform;
	if (SceneNode->GetCustomGlobalTransform(GlobalTransform))
	{
		ActorFactoryNode->SetCustomGlobalTransform(GlobalTransform);
	}
	
	FactoryNodeContainer->AddNode(ActorFactoryNode);
}

