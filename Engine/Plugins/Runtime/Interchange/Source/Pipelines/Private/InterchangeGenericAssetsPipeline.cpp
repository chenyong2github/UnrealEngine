// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericAssetsPipeline.h"

#include "Animation/Skeleton.h"
#include "CoreMinimal.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
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
	TArray<UInterchangeMeshNode*> StaticMeshNodes;
	for (const TObjectPtr<UInterchangeMeshNode> MeshNode : MeshNodes)
	{
		if (MeshNode->IsSkinnedMesh())
		{
			SkinnedMeshNodes.Add(MeshNode.Get());
		}
		else
		{
			StaticMeshNodes.Add(MeshNode.Get());
		}
	}

	if (bCombineSkeletalMeshes && SkinnedMeshNodes.Num() > 1)
	{
		//Get All the root joint nodes
		TArray<FString> SkeletonRootNodeUids;
		for (const TObjectPtr<UInterchangeSceneNode> SceneNode : SceneNodes)
		{
			if (SceneNode->IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString()))
			{
				const UInterchangeSceneNode* ParentJointNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(SceneNode->GetParentUid()));
				if (!ParentJointNode || !ParentJointNode->IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString()))
				{
					SkeletonRootNodeUids.Add(SceneNode->GetUniqueID());
				}
			}
		}
		
		TMap<FString, TArray<FString>> MesheUidsPerJointRootUid;
		for (const UInterchangeMeshNode* SkinnedMeshNode : SkinnedMeshNodes)
		{
			if (SkinnedMeshNode->GetSkeletonDependeciesCount() == 0)
			{
				continue;
			}
			FString JointNodeUid;
			SkinnedMeshNode->GetSkeletonDependency(0, JointNodeUid);
			while (!SkeletonRootNodeUids.Contains(JointNodeUid))
			{
				JointNodeUid = BaseNodeContainer->GetNode(JointNodeUid)->GetParentUid();
			}
			if (SkeletonRootNodeUids.Contains(JointNodeUid))
			{
				TArray<FString>& CombinedMeshNodes = MesheUidsPerJointRootUid.FindOrAdd(JointNodeUid);
				CombinedMeshNodes.Add(SkinnedMeshNode->GetUniqueID());
			}
		}

		const bool bLocalUseSourceNameForAsset = bUseSourceNameForAsset && (SourceDatas.Num() == 1) && MesheUidsPerJointRootUid.Num() == 1 && SkinnedMeshNodes.Num() == MeshNodes.Num();
		for (TPair<FString, TArray<FString>> JointRootUidAndMeshes : MesheUidsPerJointRootUid)
		{
			SkeletonFactoryNodes.Add(CreateSkeletonFactoryNode(JointRootUidAndMeshes.Key));
			SkeletalMeshFactoryNodes.Add(CreateSkeletalMeshFactoryNode(JointRootUidAndMeshes.Key, JointRootUidAndMeshes.Value, bLocalUseSourceNameForAsset));
		}
	}
	else
	{

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

TObjectPtr<UInterchangeSkeletonFactoryNode> UInterchangeGenericAssetsPipeline::CreateSkeletonFactoryNode(const FString& RootJointUid)
{
	const UInterchangeBaseNode* RootJointNode = BaseNodeContainer->GetNode(RootJointUid);
	if (!RootJointNode)
	{
		return nullptr;
	}
	FString DisplayLabel = RootJointNode->GetDisplayLabel() + TEXT("_Skeleton");
	FString SkeletonUid = TEXT("\\Skeleton\\") + RootJointNode->GetUniqueID();

	UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = nullptr;
	if (BaseNodeContainer->IsNodeUidValid(SkeletonUid))
	{
		//The node already exist, just return it
		SkeletonFactoryNode = Cast<UInterchangeSkeletonFactoryNode>(BaseNodeContainer->GetNode(SkeletonUid));
		if (!ensure(SkeletonFactoryNode))
		{
			//Log an error
			return nullptr;
		}
	}
	else
	{
		SkeletonFactoryNode = NewObject<UInterchangeSkeletonFactoryNode>(BaseNodeContainer.Get(), NAME_None);
		if (!ensure(SkeletonFactoryNode))
		{
			return nullptr;
		}
		SkeletonFactoryNode->InitializeSkeletonNode(SkeletonUid, DisplayLabel, USkeleton::StaticClass()->GetName());
		SkeletonFactoryNode->SetCustomRootJointUid(RootJointNode->GetUniqueID());
		BaseNodeContainer->AddNode(SkeletonFactoryNode);
	}
	return SkeletonFactoryNode;
}

TObjectPtr<UInterchangeSkeletalMeshFactoryNode> UInterchangeGenericAssetsPipeline::CreateSkeletalMeshFactoryNode(const FString& RootJointUid, TArray<FString>& MeshNodeUids, bool bLocalUseSourceNameForAsset)
{
	//Get the skeleton factory node
	const UInterchangeBaseNode* RootJointNode = BaseNodeContainer->GetNode(RootJointUid);
	if (!RootJointNode)
	{
		return nullptr;
	}
	const FString SkeletonUid = TEXT("\\Skeleton\\") + RootJointNode->GetUniqueID();
	const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = Cast<UInterchangeSkeletonFactoryNode>(BaseNodeContainer->GetNode(SkeletonUid));
	if (!ensure(SkeletonFactoryNode))
	{
		//Log an error
		return nullptr;
	}
	
	if (MeshNodeUids.Num() == 0)
	{
		return nullptr;
	}
	const UInterchangeMeshNode* FirstMeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(MeshNodeUids[0]));
	if (!FirstMeshNode)
	{
		return nullptr;
	}

	//Create the skeletal mesh factory node, name it according to the first mesh node compositing the meshes
	FString DisplayLabel = FirstMeshNode->GetDisplayLabel();
	if (bLocalUseSourceNameForAsset && SourceDatas.Num() > 0)
	{
		DisplayLabel = FPaths::GetBaseFilename(SourceDatas[0]->GetFilename());
	}
	const FString SkeletalMeshUid = TEXT("\\SkeletalMesh\\") + RootJointNode->GetUniqueID();

	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = NewObject<UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer.Get(), NAME_None);
	if (!ensure(SkeletalMeshFactoryNode))
	{
		return nullptr;
	}
	SkeletalMeshFactoryNode->InitializeSkeletalMeshNode(SkeletalMeshUid, DisplayLabel, USkeletalMesh::StaticClass()->GetName());
	SkeletalMeshFactoryNode->SetFactoryDependencyUid(SkeletonUid);
	BaseNodeContainer->AddNode(SkeletalMeshFactoryNode);

	TMap< FString, TMap<int32, TArray<FString>>> SceneInstanceUidsPerLodIndexPerMeshNodeUid;
	for (const FString& MeshNodeUid : MeshNodeUids)
	{
		const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(MeshNodeUid));
		if (!ensure(MeshNode))
		{
			continue;
		}
		//Get all scene nodes that instance this mesh node with the LOD index in case its under a LodGroup
		TMap<int32, TArray<FString>>& SceneInstanceUidsPerLodIndex = SceneInstanceUidsPerLodIndexPerMeshNodeUid.FindOrAdd(MeshNodeUid);
		{
			TArray<FString> MeshSceneNodeInstanceUids;
			MeshNode->GetSceneInstanceUids(MeshSceneNodeInstanceUids);
			for (const FString& SceneNodeUid : MeshSceneNodeInstanceUids)
			{
				if (UInterchangeSceneNode* MeshSceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(SceneNodeUid)))
				{
					UInterchangeSceneNode* ParentMeshSceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(MeshSceneNode->GetParentUid()));
					if (!ParentMeshSceneNode)
					{
						continue;
					}
					int32 SceneNodeLodIndex = 0;
					FString LastChildUid = MeshSceneNode->GetUniqueID();
					do
					{
						if (MeshSceneNode->IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetLodGroupSpecializeTypeString()))
						{
							TArray<FString> LodGroupChildrens = BaseNodeContainer->GetNodeChildrenUids(ParentMeshSceneNode->GetUniqueID());
							for (int32 LodIndex = 0; LodIndex < LodGroupChildrens.Num(); ++LodIndex)
							{
								const FString& ChildrenUid = LodGroupChildrens[LodIndex];
								if (ChildrenUid.Equals(LastChildUid))
								{
									SceneNodeLodIndex = LodIndex;
									break;
								}
							}
							break;
						}
						LastChildUid = ParentMeshSceneNode->GetUniqueID();
						ParentMeshSceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(ParentMeshSceneNode->GetParentUid()));
					} while (ParentMeshSceneNode);

					//Apply bImportLod option, it will not add LOD
					if (bImportLods || SceneNodeLodIndex == 0)
					{
						TArray<FString>& SceneInstanceUids = SceneInstanceUidsPerLodIndex.FindOrAdd(SceneNodeLodIndex);
						SceneInstanceUids.AddUnique(SceneNodeUid);
					}
				}
			}
		}
	}

	for (const TPair< FString, TMap<int32, TArray<FString>>>& MeshNodeUidAndSceneInstanceUidsPerLodIndex : SceneInstanceUidsPerLodIndexPerMeshNodeUid)
	{
		const FString& MeshNodeUid = MeshNodeUidAndSceneInstanceUidsPerLodIndex.Key;
		const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(MeshNodeUid));
		if (!ensure(MeshNode))
		{
			continue;
		}

		const TMap<int32, TArray<FString>>& SceneInstanceUidsPerLodIndex = MeshNodeUidAndSceneInstanceUidsPerLodIndex.Value;
		if (bBakeMeshes && SceneInstanceUidsPerLodIndex.Num() > 0)
		{
			//Baked and instanced meshes code path
			AddLodDataToSkeletalMesh(SkeletonFactoryNode, SkeletalMeshFactoryNode, SceneInstanceUidsPerLodIndex);
		}
		else
		{
			//Unbaked or un-instanced meshes code path
			AddLodDataToSkeletalMesh(SkeletonFactoryNode, SkeletalMeshFactoryNode, MeshNode);
		}
	}

	return SkeletalMeshFactoryNode;
}

TObjectPtr<UInterchangeSkeletalMeshLodDataNode> UInterchangeGenericAssetsPipeline::CreateSkeletalMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID)
{
	FString DisplayLabel(NodeName);
	FString NodeUID(NodeUniqueID);
	UInterchangeSkeletalMeshLodDataNode* SkeletalMeshLodDataNode = NewObject<UInterchangeSkeletalMeshLodDataNode>(BaseNodeContainer.Get(), NAME_None);
	if (!ensure(SkeletalMeshLodDataNode))
	{
		//TODO log error
		return nullptr;
	}
	// Creating a UMaterialInterface
	SkeletalMeshLodDataNode->InitializeNode(NodeUID, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_FactoryData);
	BaseNodeContainer->AddNode(SkeletalMeshLodDataNode);
	return SkeletalMeshLodDataNode;
}

void UInterchangeGenericAssetsPipeline::AddLodDataToSkeletalMesh(const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode, const TMap<int32, TArray<FString>>& SceneInstanceUidsPerLodIndex)
{
	const FString SkeletalMeshUid = SkeletalMeshFactoryNode->GetUniqueID();
	const FString SkeletonUid = SkeletonFactoryNode->GetUniqueID();
	FString RootjointNodeUid;
	SkeletonFactoryNode->GetCustomRootJointUid(RootjointNodeUid);
	for (const TPair<int32, TArray<FString>>& LodIndexAndSceneInstanceUids : SceneInstanceUidsPerLodIndex)
	{
		const int32 LodIndex = LodIndexAndSceneInstanceUids.Key;
		const TArray<FString>& SceneNodeUids = LodIndexAndSceneInstanceUids.Value;

		//Create a lod data node with all the meshes for this LOD
		const FString SkeletalMeshLodDataName = TEXT("LodData") + FString::FromInt(LodIndex);
		const FString LODDataPrefix = TEXT("\\LodData") + (LodIndex > 0 ? FString::FromInt(LodIndex) : TEXT("")) + TEXT("\\");
		const FString SkeletalMeshLodDataUniqueID = LODDataPrefix + RootjointNodeUid;
		//The LodData already exist
		UInterchangeSkeletalMeshLodDataNode* LodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(BaseNodeContainer->GetNode(SkeletalMeshLodDataUniqueID));
		if (!LodDataNode)
		{
			//Add the data for the LOD (skeleton Unique ID and all the mesh node fbx path, so we can find them when we will create the payload data)
			LodDataNode = CreateSkeletalMeshLodDataNode(SkeletalMeshLodDataName, SkeletalMeshLodDataUniqueID);
			LodDataNode->SetParentUid(SkeletalMeshUid);
			LodDataNode->SetCustomSkeletonUid(SkeletonUid);
			SkeletalMeshFactoryNode->AddLodDataUniqueId(SkeletalMeshLodDataUniqueID);
		}
		for (const FString& SceneNodeUid : SceneNodeUids)
		{
			const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(SceneNodeUid));
			TArray<FString> MaterialDependencies;
			SceneNode->GetMaterialDependencyUids(MaterialDependencies);
			const int32 MaterialCount = MaterialDependencies.Num();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				const FString MaterialFactoryNodeUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialDependencies[MaterialIndex]);
				//Create a factory dependency so Material asset are import before the skeletal mesh asset
				SkeletalMeshFactoryNode->SetFactoryDependencyUid(MaterialFactoryNodeUid);
			}
			//Add the scene node has a reference to bake all mesh instance
			LodDataNode->AddMeshUid(SceneNodeUid);
		}
	}
}

void UInterchangeGenericAssetsPipeline::AddLodDataToSkeletalMesh(const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode, const UInterchangeMeshNode* MeshNode)
{
	const FString SkeletalMeshUid = SkeletalMeshFactoryNode->GetUniqueID();
	const FString SkeletonUid = SkeletonFactoryNode->GetUniqueID();
	FString RootjointNodeUid;
	SkeletonFactoryNode->GetCustomRootJointUid(RootjointNodeUid);
	//simply export the mesh node to lod 0
	//Create a lod data node with all the meshes for this LOD
	const FString SkeletalMeshLodDataName = TEXT("LodData0");
	const FString SkeletalMeshLodDataUniqueID = TEXT("\\LodData\\") + RootjointNodeUid;
	UInterchangeSkeletalMeshLodDataNode* LodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(BaseNodeContainer->GetNode(SkeletalMeshLodDataUniqueID));
	if (!LodDataNode)
	{
		//Add the data for the LOD (skeleton Unique ID and all the mesh node fbx path, so we can find them when we will create the payload data)
		LodDataNode = CreateSkeletalMeshLodDataNode(SkeletalMeshLodDataName, SkeletalMeshLodDataUniqueID);
		LodDataNode->SetParentUid(SkeletalMeshUid);
		LodDataNode->SetCustomSkeletonUid(SkeletonUid);
		SkeletalMeshFactoryNode->AddLodDataUniqueId(SkeletalMeshLodDataUniqueID);
	}
	TArray<FString> MaterialDependencies;
	MeshNode->GetMaterialDependencies(MaterialDependencies);
	const int32 MaterialCount = MaterialDependencies.Num();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		const FString MaterialFactoryNodeUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialDependencies[MaterialIndex]);
		//Create a factory dependency so Material asset are import before the skeletal mesh asset
		SkeletalMeshFactoryNode->SetFactoryDependencyUid(MaterialFactoryNodeUid);
	}
	LodDataNode->AddMeshUid(MeshNode->GetUniqueID());
}