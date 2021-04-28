// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericAssetsPipeline.h"

#include "CoreMinimal.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangeSceneNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangeSourceData.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

bool UInterchangeGenericAssetsPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericAssetsPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return false;
	}

	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas.Empty(InSourceDatas.Num());
	for (const UInterchangeSourceData* SourceData : InSourceDatas)
	{
		SourceDatas.Add(SourceData);
	}

	
	//Find all translated node we need for this pipeline
	BaseNodeContainer->IterateNodes([this](const FString& NodeUid, UInterchangeBaseNode* Node)
	{
		switch(Node->GetnodeContainerType())
		{
			case EInterchangeNodeContainerType::NodeContainerType_TranslatedAsset:
			{
				if (UInterchangeTextureNode* TextureNode = Cast<UInterchangeTextureNode>(Node))
				{
					TextureNodes.Add(TextureNode);
				}
				else if (UInterchangeMaterialNode* MaterialNode = Cast<UInterchangeMaterialNode>(Node))
				{
					MaterialNodes.Add(MaterialNode);
				}
				else if (UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(Node))
				{
					MeshNodes.Add(MeshNode);
				}
			}
			break;
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
	
	//import textures
	for (const TObjectPtr<UInterchangeTextureNode> TextureNode : TextureNodes)
	{
		CreateTexture2DFactoryNode(TextureNode);
	}
	//import materials
	for (const TObjectPtr<UInterchangeMaterialNode> MaterialNode : MaterialNodes)
	{
		CreateMaterialFactoryNode(MaterialNode);
	}

	//Find all skinned mesh
	//import meshes
	TArray<UInterchangeMeshNode*> SkinnedMeshNodes;
	for (const TObjectPtr<UInterchangeMeshNode> MeshNode : MeshNodes)
	{
		if (MeshNode->IsSkinnedMesh())
		{
			SkinnedMeshNodes.Add(MeshNode.Get());
		}
	}

	return true;
}

TObjectPtr<UInterchangeTextureFactoryNode> UInterchangeGenericAssetsPipeline::CreateTexture2DFactoryNode(const TObjectPtr<UInterchangeTextureNode> TextureNode)
{
	FString DisplayLabel = TextureNode->GetDisplayLabel();
	FString NodeUid = UInterchangeTextureFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TextureNode->GetUniqueID());
	UInterchangeTextureFactoryNode* TextureFactoryNode = nullptr;
	if (BaseNodeContainer->IsNodeUidValid(NodeUid))
	{
		TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(BaseNodeContainer->GetNode(NodeUid));
		if (!ensure(TextureFactoryNode))
		{
			//Log an error
		}
	}
	else
	{
		TextureFactoryNode = NewObject<UInterchangeTextureFactoryNode>(BaseNodeContainer.Get(), NAME_None);
		if (!ensure(TextureFactoryNode))
		{
			return nullptr;
		}
		//Creating a UTexture2D
		TextureFactoryNode->InitializeTextureNode(NodeUid, DisplayLabel, UTexture2D::StaticClass()->GetName(), TextureNode->GetDisplayLabel());
		TextureFactoryNode->SetCustomTranslatedTextureNodeUid(TextureNode->GetUniqueID());
		BaseNodeContainer->AddNode(TextureFactoryNode);
		TextureFactoryNodes.Add(TextureFactoryNode);
	}
	return TextureFactoryNode;
}

TObjectPtr<UInterchangeMaterialFactoryNode> UInterchangeGenericAssetsPipeline::CreateMaterialFactoryNode(const TObjectPtr<UInterchangeMaterialNode> MaterialNode)
{
	FString DisplayLabel = MaterialNode->GetDisplayLabel();
	FString NodeUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialNode->GetUniqueID());
	UInterchangeMaterialFactoryNode* MaterialFactoryNode = nullptr;
	if (BaseNodeContainer->IsNodeUidValid(NodeUid))
	{
		//The node already exist, just return it
		MaterialFactoryNode = Cast<UInterchangeMaterialFactoryNode>(BaseNodeContainer->GetNode(NodeUid));
		if (!ensure(MaterialFactoryNode))
		{
			//Log an error
		}
	}
	else
	{
		MaterialFactoryNode = NewObject<UInterchangeMaterialFactoryNode>(BaseNodeContainer.Get(), NAME_None);
		if (!ensure(MaterialFactoryNode))
		{
			return nullptr;
		}
		//Creating a Material
		MaterialFactoryNode->InitializeMaterialNode(NodeUid, DisplayLabel, UMaterial::StaticClass()->GetName());
		MaterialFactoryNode->SetCustomTranslatedMaterialNodeUid(MaterialNode->GetUniqueID());
		TArray<FString> TranslatedDependencies;
		MaterialNode->GetTextureDependencies(TranslatedDependencies);
		for (const FString& DepUid : TranslatedDependencies)
		{
			FString FactoryDepUid = UInterchangeTextureFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(DepUid);
			if (UInterchangeTextureFactoryNode* TextureDep = Cast<UInterchangeTextureFactoryNode>(BaseNodeContainer->GetNode(FactoryDepUid)))
			{
				//We found a factory dependency, because we need to be able to retrieve the UObject create by the factory
				MaterialFactoryNode->SetTextureDependencyUid(FactoryDepUid);
				//We also add it to the base dependencies so the texture factory will be called before this material factory
				MaterialFactoryNode->SetFactoryDependencyUid(FactoryDepUid);
			}
		}
		BaseNodeContainer->AddNode(MaterialFactoryNode);
		MaterialFactoryNodes.Add(MaterialFactoryNode);
	}
	return MaterialFactoryNode;
}

TObjectPtr<UInterchangeBaseNode> UInterchangeGenericAssetsPipeline::CreateMeshFactoryNode(const TObjectPtr<UInterchangeMeshNode> MeshNode)
{
	FString DisplayLabel = MeshNode->GetDisplayLabel();
	FString NodeUid = MeshNode->GetUniqueID();
	return nullptr;
}