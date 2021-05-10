// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericAssetsPipeline.h"

#include "Animation/Skeleton.h"
#include "CoreMinimal.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangePipelineMeshesUtilities.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangeSourceData.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
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

	PipelineMeshesUtilities = UInterchangePipelineMeshesUtilities::CreateInterchangePipelineMeshesUtilities(BaseNodeContainer);
	TArray<UInterchangeMeshNode*> SkinnedMeshNodes;
	TArray<UInterchangeMeshNode*> StaticMeshNodes;
	TMap<FString, TArray<FString>> SkeletalMeshFactoryDependencyOrderPerSkeletonRootNodeUid;
	
	//Find all translated node we need for this pipeline
	BaseNodeContainer->IterateNodes([this, &SkinnedMeshNodes, &StaticMeshNodes](const FString& NodeUid, UInterchangeBaseNode* Node)
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
					if (MeshNode->IsSkinnedMesh())
					{
						SkinnedMeshNodes.Add(MeshNode);
					}
					else
					{
						StaticMeshNodes.Add(MeshNode);
					}

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
	for (const UInterchangeTextureNode* TextureNode : TextureNodes)
	{
		CreateTexture2DFactoryNode(TextureNode);
	}
	//import materials
	for (const UInterchangeMaterialNode* MaterialNode : MaterialNodes)
	{
		CreateMaterialFactoryNode(MaterialNode);
	}


	if (SkinnedMeshNodes.Num() > 0)
	{
		auto SetSkeletalMeshDependencies = [&SkeletalMeshFactoryDependencyOrderPerSkeletonRootNodeUid](const FString& JointNodeUid, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode)
		{
			TArray<FString>& SkeletalMeshFactoryDependencyOrder = SkeletalMeshFactoryDependencyOrderPerSkeletonRootNodeUid.FindOrAdd(JointNodeUid);
			//Updating the skeleton is not multi thread safe, so we add dependency between skeletalmesh altering the same skeleton
			//TODO make the skeletalMesh ReferenceSkeleton thread safe to allow multiple parallel skeletalmesh factory on the same skeleton asset.
			int32 DependencyIndex = SkeletalMeshFactoryDependencyOrder.AddUnique(SkeletalMeshFactoryNode->GetUniqueID());
			if (DependencyIndex > 0)
			{
				const FString SkeletalMeshFactoryNodeDependencyUid = SkeletalMeshFactoryDependencyOrder[DependencyIndex - 1];
				SkeletalMeshFactoryNode->SetFactoryDependencyUid(SkeletalMeshFactoryNodeDependencyUid);
			}
		};

		if (bCombineSkeletalMeshes)
		{
			//////////////////////////////////////////////////////////////////////////
			//Combined everything we can
			TMap<FString, TArray<FString>> MeshUidsPerSkeletonRootUid;
			auto CreatePerSkeletonRootUidCombinedSkinnedMesh = [this, &MeshUidsPerSkeletonRootUid, &SetSkeletalMeshDependencies](const bool bUseInstanceMesh)
			{
				bool bFoundInstances = false;
				for (const TPair<FString, TArray<FString>>& SkeletonRootUidAndMeshUids : MeshUidsPerSkeletonRootUid)
				{
					const FString& SkeletonRootUid = SkeletonRootUidAndMeshUids.Key;
					//Every iteration is a skeletalmesh asset that combine all MeshInstances sharing the same skeleton root node
					UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = CreateSkeletonFactoryNode(SkeletonRootUid);
					//The MeshUids can represent a SceneNode pointing on a MeshNode or directly a MeshNode;
					TMap<int32, TArray<FString>> MeshUidsPerLodIndex;
					const TArray<FString>& MeshUids = SkeletonRootUidAndMeshUids.Value;
					for (const FString& MeshUid : MeshUids)
					{
						if (bUseInstanceMesh)
						{
							const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
							for (const TPair<int32, FInterchangeLodSceneNodeContainer>& LodIndexAndSceneNodeContainer : MeshInstance.SceneNodePerLodIndex)
							{
								const int32 LodIndex = LodIndexAndSceneNodeContainer.Key;
								const FInterchangeLodSceneNodeContainer& SceneNodeContainer = LodIndexAndSceneNodeContainer.Value;
								TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
								for (const UInterchangeSceneNode* SceneNode : SceneNodeContainer.SceneNodes)
								{
									TranslatedNodes.Add(SceneNode->GetUniqueID());
								}
							}
						}
						else
						{
							//MeshGeometry cannot have Lod since LODs are define in the scene node
							const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(MeshUid);
							const int32 LodIndex = 0;
							TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
							TranslatedNodes.Add(MeshGeometry.MeshUid);
						}
					}

					if (MeshUidsPerLodIndex.Num() > 0)
					{
						UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = CreateSkeletalMeshFactoryNode(SkeletonRootUid, MeshUidsPerLodIndex);
						SetSkeletalMeshDependencies(SkeletonRootUid, SkeletalMeshFactoryNode);
						SkeletonFactoryNodes.Add(SkeletonFactoryNode);
						SkeletalMeshFactoryNodes.Add(SkeletalMeshFactoryNode);
						bFoundInstances = true;
					}
				}
				return bFoundInstances;
			};

			bool bFoundMeshes = false;
			if (bBakeMeshes)
			{
				PipelineMeshesUtilities->GetCombinedSkinnedMeshInstances(MeshUidsPerSkeletonRootUid);
				const bool bUseMeshInstance = true;
				bFoundMeshes = CreatePerSkeletonRootUidCombinedSkinnedMesh(bUseMeshInstance);
			}

			if (!bFoundMeshes)
			{
				MeshUidsPerSkeletonRootUid.Empty();
				PipelineMeshesUtilities->GetCombinedSkinnedMeshGeometries(MeshUidsPerSkeletonRootUid);
				const bool bUseMeshInstance = false;
				CreatePerSkeletonRootUidCombinedSkinnedMesh(bUseMeshInstance);
			}
		}
		else
		{
			//////////////////////////////////////////////////////////////////////////
			//Do not combined meshes
			TArray<FString> MeshUids;
			auto CreatePerSkeletonRootUidSkinnedMesh = [this, &MeshUids, &SetSkeletalMeshDependencies](const bool bUseInstanceMesh)
			{
				bool bFoundInstances = false;
				for (const FString& MeshUid : MeshUids)
				{
					//Every iteration is a skeletalmesh asset that combine all MeshInstances sharing the same skeleton root node
					//The MeshUids can represent a SceneNode pointing on a MeshNode or directly a MeshNode;
					TMap<int32, TArray<FString>> MeshUidsPerLodIndex;
					FString SkeletonRootUid;
					if (!(bUseInstanceMesh ? PipelineMeshesUtilities->IsValidMeshInstanceUid(MeshUid) : PipelineMeshesUtilities->IsValidMeshGeometryUid(MeshUid)))
					{
						continue;
					}
					SkeletonRootUid = (bUseInstanceMesh ? PipelineMeshesUtilities->GetMeshInstanceSkeletonRootUid(MeshUid) : PipelineMeshesUtilities->GetMeshGeometrySkeletonRootUid(MeshUid));
					if (SkeletonRootUid.IsEmpty())
					{
						//Log an error
						continue;
					}
					UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = CreateSkeletonFactoryNode(SkeletonRootUid);
					if (bUseInstanceMesh)
					{
						const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
						for (const TPair<int32, FInterchangeLodSceneNodeContainer>& LodIndexAndSceneNodeContainer : MeshInstance.SceneNodePerLodIndex)
						{
							const int32 LodIndex = LodIndexAndSceneNodeContainer.Key;
							const FInterchangeLodSceneNodeContainer& SceneNodeContainer = LodIndexAndSceneNodeContainer.Value;
							TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
							for (const UInterchangeSceneNode* SceneNode : SceneNodeContainer.SceneNodes)
							{
								TranslatedNodes.Add(SceneNode->GetUniqueID());
							}
						}
					}
					else
					{
						const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(MeshUid);
						const int32 LodIndex = 0;
						TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
						TranslatedNodes.Add(MeshGeometry.MeshUid);
					}
					if (MeshUidsPerLodIndex.Num() > 0)
					{
						UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = CreateSkeletalMeshFactoryNode(SkeletonRootUid, MeshUidsPerLodIndex);
						SetSkeletalMeshDependencies(SkeletonRootUid, SkeletalMeshFactoryNode);
						SkeletonFactoryNodes.Add(SkeletonFactoryNode);
						SkeletalMeshFactoryNodes.Add(SkeletalMeshFactoryNode);
						bFoundInstances = true;
					}
				}
				return bFoundInstances;
			};
			
			bool bFoundMeshes = false;
			if (bBakeMeshes)
			{
				PipelineMeshesUtilities->GetAllSkinnedMeshInstance(MeshUids);
				const bool bUseMeshInstance = true;
				bFoundMeshes = CreatePerSkeletonRootUidSkinnedMesh(bUseMeshInstance);
			}

			if (!bFoundMeshes)
			{
				MeshUids.Empty();
				PipelineMeshesUtilities->GetAllSkinnedMeshGeometry(MeshUids);
				const bool bUseMeshInstance = false;
				CreatePerSkeletonRootUidSkinnedMesh(bUseMeshInstance);
			}
		}
	}

	const UClass* SkeletalMeshFactoryNodeClass = UInterchangeSkeletalMeshFactoryNode::StaticClass();
	TArray<FString> SkeletalMeshNodeUids;
	BaseNodeContainer->GetNodes(SkeletalMeshFactoryNodeClass, SkeletalMeshNodeUids);

// 	const UClass* StaticMeshFactoryNodeClass = UInterchangeStaticMeshFactoryNode::StaticClass();
// 	TArray<FString> StaticMeshNodeUids;
// 	BaseNodeContainer->GetNodes(StaticMeshFactoryNodeClass, StaticMeshNodeUids);

	//TODO count also the imported animations

	const int32 MeshesAndAnimsImportedNodeCount = SkeletalMeshNodeUids.Num(); // + StaticMeshNodeUids.Num();
	if (bUseSourceNameForAsset && MeshesAndAnimsImportedNodeCount == 1)
	{
		if (SkeletalMeshNodeUids.Num() > 0)
		{
			UInterchangeSkeletalMeshFactoryNode* SkeletalMeshNode = Cast<UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer->GetNode(SkeletalMeshNodeUids[0]));
			const FString DisplayLabelName = FPaths::GetBaseFilename(SourceDatas[0]->GetFilename());
			SkeletalMeshNode->SetDisplayLabel(DisplayLabelName);
		}
	}

	return true;
}

UInterchangeTextureFactoryNode* UInterchangeGenericAssetsPipeline::CreateTexture2DFactoryNode(const UInterchangeTextureNode* TextureNode)
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
		TextureFactoryNode = NewObject<UInterchangeTextureFactoryNode>(BaseNodeContainer, NAME_None);
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

UInterchangeMaterialFactoryNode* UInterchangeGenericAssetsPipeline::CreateMaterialFactoryNode(const UInterchangeMaterialNode* MaterialNode)
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
		MaterialFactoryNode = NewObject<UInterchangeMaterialFactoryNode>(BaseNodeContainer, NAME_None);
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

UInterchangeSkeletonFactoryNode* UInterchangeGenericAssetsPipeline::CreateSkeletonFactoryNode(const FString& RootJointUid)
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
		SkeletonFactoryNode = NewObject<UInterchangeSkeletonFactoryNode>(BaseNodeContainer, NAME_None);
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

UInterchangeSkeletalMeshFactoryNode* UInterchangeGenericAssetsPipeline::CreateSkeletalMeshFactoryNode(const FString& RootJointUid, const TMap<int32, TArray<FString>> MeshUidsPerLodIndex)
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
	
	if (MeshUidsPerLodIndex.Num() == 0)
	{
		return nullptr;
	}
	
	auto GetFirstNodeInfo = [this, &MeshUidsPerLodIndex](const int32 Index, FString& OutFirstMeshNodeUid, int32& OutSceneNodeCount)->const UInterchangeBaseNode*
	{
		OutSceneNodeCount = 0;
		if (!ensure(Index >= 0 && MeshUidsPerLodIndex.Num() > Index))
		{
			//Log an error
			return nullptr;
		}
		for (const TPair<int32, TArray<FString>>& LodIndexAndMeshUids : MeshUidsPerLodIndex)
		{
			if (Index == LodIndexAndMeshUids.Key)
			{
				const TArray<FString>& MeshUids = LodIndexAndMeshUids.Value;
				if (MeshUids.Num() > 0)
				{
					const FString& MeshUid = MeshUids[0];
					UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(MeshUid));
					if (MeshNode)
					{
						OutFirstMeshNodeUid = MeshUid;
						return MeshNode;
					}
					UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(MeshUid));
					if (SceneNode)
					{
						FString MeshNodeUid;
						if (SceneNode->GetCustomMeshDependencyUid(MeshNodeUid))
						{
							OutSceneNodeCount = MeshUids.Num();
							OutFirstMeshNodeUid = MeshNodeUid;
							return SceneNode;
						}
					}
				}
				//We found the lod but there is no valid Mesh node to return the Uid
				break;
			}
		}
		return nullptr;
	};

	FString FirstMeshNodeUid;
	const int32 BaseLodIndex = 0;
	int32 SceneNodeCount = 0;
	const UInterchangeBaseNode* InterchangeBaseNode = GetFirstNodeInfo(BaseLodIndex, FirstMeshNodeUid, SceneNodeCount);
	if (!InterchangeBaseNode)
	{
		//Log an error
		return nullptr;
	}
	const UInterchangeSceneNode* FirstSceneNode = Cast<UInterchangeSceneNode>(InterchangeBaseNode);
	const UInterchangeMeshNode* FirstMeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(FirstMeshNodeUid));

	//Create the skeletal mesh factory node, name it according to the first mesh node compositing the meshes
	FString DisplayLabel = FirstMeshNode->GetDisplayLabel();
	FString SkeletalMeshUid_MeshNamePart = FirstMeshNodeUid;
	if(FirstSceneNode)
	{
		//If we are instancing one scene node, we want to use it to name the mesh
		if (SceneNodeCount == 1)
		{
			DisplayLabel = FirstSceneNode->GetDisplayLabel();
		}
		//Use the first scene node uid this skeletalmesh reference, add backslash since this uid is not asset typed (\\Mesh\\) like FirstMeshNodeUid
		SkeletalMeshUid_MeshNamePart = TEXT("\\") + FirstSceneNode->GetUniqueID();
	}
	const FString SkeletalMeshUid = TEXT("\\SkeletalMesh") + SkeletalMeshUid_MeshNamePart + SkeletonUid;
	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = NewObject<UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer, NAME_None);
	if (!ensure(SkeletalMeshFactoryNode))
	{
		return nullptr;
	}
	SkeletalMeshFactoryNode->InitializeSkeletalMeshNode(SkeletalMeshUid, DisplayLabel, USkeletalMesh::StaticClass()->GetName());
	SkeletalMeshFactoryNode->SetFactoryDependencyUid(SkeletonUid);
	BaseNodeContainer->AddNode(SkeletalMeshFactoryNode);

	AddLodDataToSkeletalMesh(SkeletonFactoryNode, SkeletalMeshFactoryNode, MeshUidsPerLodIndex);

	return SkeletalMeshFactoryNode;
}

UInterchangeSkeletalMeshLodDataNode* UInterchangeGenericAssetsPipeline::CreateSkeletalMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID)
{
	FString DisplayLabel(NodeName);
	FString NodeUID(NodeUniqueID);
	UInterchangeSkeletalMeshLodDataNode* SkeletalMeshLodDataNode = NewObject<UInterchangeSkeletalMeshLodDataNode>(BaseNodeContainer, NAME_None);
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

void UInterchangeGenericAssetsPipeline::AddLodDataToSkeletalMesh(const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex)
{
	const FString SkeletalMeshUid = SkeletalMeshFactoryNode->GetUniqueID();
	const FString SkeletonUid = SkeletonFactoryNode->GetUniqueID();
	FString RootjointNodeUid;
	SkeletonFactoryNode->GetCustomRootJointUid(RootjointNodeUid);
	for (const TPair<int32, TArray<FString>>& LodIndexAndNodeUids : NodeUidsPerLodIndex)
	{
		const int32 LodIndex = LodIndexAndNodeUids.Key;
		const TArray<FString>& NodeUids = LodIndexAndNodeUids.Value;

		//Create a lod data node with all the meshes for this LOD
		const FString SkeletalMeshLodDataName = TEXT("LodData") + FString::FromInt(LodIndex);
		const FString LODDataPrefix = TEXT("\\LodData") + (LodIndex > 0 ? FString::FromInt(LodIndex) : TEXT(""));
		const FString SkeletalMeshLodDataUniqueID = LODDataPrefix + SkeletalMeshUid + SkeletonUid;
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
		for (const FString& NodeUid : NodeUids)
		{
			TArray<FString> MaterialDependencies;
			if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				SceneNode->GetMaterialDependencyUids(MaterialDependencies);
			}
			else if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				MeshNode->GetMaterialDependencies(MaterialDependencies);
			}
			const int32 MaterialCount = MaterialDependencies.Num();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				const FString MaterialFactoryNodeUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialDependencies[MaterialIndex]);
				//Create a factory dependency so Material asset are import before the skeletal mesh asset
				TArray<FString> FactoryDependencies;
				SkeletalMeshFactoryNode->GetFactoryDependencies(FactoryDependencies);
				if (!FactoryDependencies.Contains(MaterialFactoryNodeUid))
				{
					SkeletalMeshFactoryNode->SetFactoryDependencyUid(MaterialFactoryNodeUid);
				}
			}
			LodDataNode->AddMeshUid(NodeUid);
		}
	}
}
