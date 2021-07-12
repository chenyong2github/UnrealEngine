// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericAssetsPipeline.h"

#include "Animation/Skeleton.h"
#include "CoreMinimal.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureLightProfile.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangePipelineMeshesUtilities.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeStaticMeshFactoryNode.h"
#include "InterchangeStaticMeshLodDataNode.h"
#include "InterchangeTexture2DArrayFactoryNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTextureCubeFactoryNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureLightProfileFactoryNode.h"
#include "InterchangeTextureLightProfileNode.h"
#include "InterchangeTextureNode.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

namespace UE::Interchange::Private
{
	UClass* GetDefaultFactoryClassFromTextureNodeClass(UClass* NodeClass)
	{
		if (UInterchangeTexture2DNode::StaticClass() == NodeClass)
		{
			return UInterchangeTextureFactoryNode::StaticClass();
		}

		if (UInterchangeTextureCubeNode::StaticClass() == NodeClass)
		{
			return UInterchangeTextureCubeFactoryNode::StaticClass();
		}

		if (UInterchangeTexture2DArrayNode::StaticClass() == NodeClass)
		{
			return UInterchangeTexture2DArrayFactoryNode::StaticClass();
		}

		if (UInterchangeTextureLightProfileNode::StaticClass() == NodeClass)
		{
			return UInterchangeTextureLightProfileFactoryNode::StaticClass();
		}

		return nullptr;
	}

	UClass* GetDefaultAssetClassFromFactoryClass(UClass* NodeClass)
	{
		if (UInterchangeTextureFactoryNode::StaticClass() == NodeClass)
		{
			return UTexture2D::StaticClass();
		}

		if (UInterchangeTextureCubeFactoryNode::StaticClass() == NodeClass)
		{
			return UTextureCube::StaticClass();
		}

		if (UInterchangeTexture2DArrayFactoryNode::StaticClass() == NodeClass)
		{
			return UTexture2DArray::StaticClass();
		}

		if (UInterchangeTextureLightProfileFactoryNode::StaticClass() == NodeClass)
		{
			return UTextureLightProfile::StaticClass();
		}

		return nullptr;
	}
}


bool UInterchangeGenericAssetsPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	// @TODO: break up this method into multiple smaller ones, before it becomes more of a monster!

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
	
	if (bImportTextures)
	{
		//import textures
		for (const UInterchangeTextureNode* TextureNode : TextureNodes)
		{
			CreateTextureFactoryNode(TextureNode, UE::Interchange::Private::GetDefaultFactoryClassFromTextureNodeClass(TextureNode->GetClass()));
		}
	}

	if (bImportMaterials)
	{
		//import materials
		for (const UInterchangeMaterialNode* MaterialNode : MaterialNodes)
		{
			CreateMaterialFactoryNode(MaterialNode);
		}
	}


	if (bImportSkeletalMeshes && SkinnedMeshNodes.Num() > 0)
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
				SkeletalMeshFactoryNode->AddFactoryDependencyUid(SkeletalMeshFactoryNodeDependencyUid);
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

	if (bImportStaticMeshes && StaticMeshNodes.Num() > 0)
	{
		if (bCombineStaticMeshes)
		{
			// Combine all the static meshes

			bool bFoundMeshes = false;
			if (bBakeMeshes)
			{
				// If baking transforms, get all the static mesh instance nodes, and group them by LOD
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshInstance(MeshUids);

				TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

				for (const FString& MeshUid : MeshUids)
				{
					const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
					for (const auto& LodIndexAndSceneNodeContainer : MeshInstance.SceneNodePerLodIndex)
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

				// If we got some instances, create a static mesh factory node
				if (MeshUidsPerLodIndex.Num() > 0)
				{
					UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
					StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
					bFoundMeshes = true;
				}
			}

			if (!bFoundMeshes)
			{
				// If we haven't yet managed to build a factory node, look at static mesh geometry directly.
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshGeometry(MeshUids);

				TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

				for (const FString& MeshUid : MeshUids)
				{
					// MeshGeometry cannot have Lod since LODs are defined in the scene node
					const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(MeshUid);
					const int32 LodIndex = 0;
					TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
					TranslatedNodes.Add(MeshGeometry.MeshUid);
				}

				if (MeshUidsPerLodIndex.Num() > 0)
				{
					UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
					StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
				}
			}

		}
		else
		{
			// Do not combine static meshes

			bool bFoundMeshes = false;
			if (bBakeMeshes)
			{
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshInstance(MeshUids);
				
				for (const FString& MeshUid : MeshUids)
				{
					// @TODO: check this is correct. What happens when we explicitly don't combine scenes with a LOD group node and multiple transform nodes?
					TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

					const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
					for (const auto& LodIndexAndSceneNodeContainer : MeshInstance.SceneNodePerLodIndex)
					{
						const int32 LodIndex = LodIndexAndSceneNodeContainer.Key;
						const FInterchangeLodSceneNodeContainer& SceneNodeContainer = LodIndexAndSceneNodeContainer.Value;

						TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
						for (const UInterchangeSceneNode* SceneNode : SceneNodeContainer.SceneNodes)
						{
							TranslatedNodes.Add(SceneNode->GetUniqueID());
						}
					}

					if (MeshUidsPerLodIndex.Num() > 0)
					{
						UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
						StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
						bFoundMeshes = true;
					}
				}
			}

			if (!bFoundMeshes)
			{
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshGeometry(MeshUids);

				for (const FString& MeshUid : MeshUids)
				{
					TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

					const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(MeshUid);
					const int32 LodIndex = 0;
					TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
					TranslatedNodes.Add(MeshGeometry.MeshUid);

					if (MeshUidsPerLodIndex.Num() > 0)
					{
						UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
						StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
					}
				}
			}
		}
	}

	const UClass* SkeletalMeshFactoryNodeClass = UInterchangeSkeletalMeshFactoryNode::StaticClass();
	TArray<FString> SkeletalMeshNodeUids;
	BaseNodeContainer->GetNodes(SkeletalMeshFactoryNodeClass, SkeletalMeshNodeUids);

 	const UClass* StaticMeshFactoryNodeClass = UInterchangeStaticMeshFactoryNode::StaticClass();
 	TArray<FString> StaticMeshNodeUids;
 	BaseNodeContainer->GetNodes(StaticMeshFactoryNodeClass, StaticMeshNodeUids);

	//TODO count also the imported animations

	//If we import only one asset, and bUseSourceNameForAsset is true, we want to rename the asset using the file name.
	const int32 MeshesAndAnimsImportedNodeCount = SkeletalMeshNodeUids.Num() + StaticMeshNodeUids.Num();
	if (bUseSourceNameForAsset && MeshesAndAnimsImportedNodeCount == 1)
	{
		if (SkeletalMeshNodeUids.Num() > 0)
		{
			UInterchangeSkeletalMeshFactoryNode* SkeletalMeshNode = Cast<UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer->GetNode(SkeletalMeshNodeUids[0]));
			const FString DisplayLabelName = FPaths::GetBaseFilename(SourceDatas[0]->GetFilename());
			SkeletalMeshNode->SetDisplayLabel(DisplayLabelName);
		}
		else if (StaticMeshNodeUids.Num() > 0)
		{
			UInterchangeStaticMeshFactoryNode* StaticMeshNode = Cast<UInterchangeStaticMeshFactoryNode>(BaseNodeContainer->GetNode(StaticMeshNodeUids[0]));
			const FString DisplayLabelName = FPaths::GetBaseFilename(SourceDatas[0]->GetFilename());
			StaticMeshNode->SetDisplayLabel(DisplayLabelName);
		}
	}

	return true;
}

UInterchangeTextureFactoryNode* UInterchangeGenericAssetsPipeline::CreateTextureFactoryNode(const UInterchangeTextureNode* TextureNode, const TSubclassOf<UInterchangeTextureFactoryNode>& FactorySubclass)
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
		UClass* FactoryClass = FactorySubclass.Get();
		if (!ensure(FactoryClass))
		{
			// Log an error
			return nullptr;
		}

		UClass* TextureClass = UE::Interchange::Private::GetDefaultAssetClassFromFactoryClass(FactoryClass);
		if (!ensure(TextureClass))
		{
			// Log an error
			return nullptr;
		}

		TextureFactoryNode = NewObject<UInterchangeTextureFactoryNode>(BaseNodeContainer, FactoryClass);
		if (!ensure(TextureFactoryNode))
		{
			return nullptr;
		}
		//Creating a UTexture2D
		TextureFactoryNode->InitializeTextureNode(NodeUid, DisplayLabel, TextureClass->GetName(), TextureNode->GetDisplayLabel());
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
				MaterialFactoryNode->AddFactoryDependencyUid(FactoryDepUid);
			}
		}
		BaseNodeContainer->AddNode(MaterialFactoryNode);
		MaterialFactoryNodes.Add(MaterialFactoryNode);
		MaterialFactoryNode->AddTargetAssetUid(MaterialNode->GetUniqueID());
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
		SkeletonFactoryNode->AddTargetAssetUid(RootJointNode->GetUniqueID());	// todo: add all joints as target assets?
		BaseNodeContainer->AddNode(SkeletonFactoryNode);
	}
	return SkeletonFactoryNode;
}


bool UInterchangeGenericAssetsPipeline::MakeMeshFactoryNodeUidAndDisplayLabel(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex, int32 LodIndex, FString& NewNodeUid, FString& DisplayLabel)
{
	int32 SceneNodeCount = 0;

	if (!ensure(LodIndex >= 0 && MeshUidsPerLodIndex.Num() > LodIndex))
	{
		return false;
	}

	for (const TPair<int32, TArray<FString>>& LodIndexAndMeshUids : MeshUidsPerLodIndex)
	{
		if (LodIndex == LodIndexAndMeshUids.Key)
		{
			const TArray<FString>& Uids = LodIndexAndMeshUids.Value;
			if (Uids.Num() > 0)
			{
				const FString& Uid = Uids[0];
				UInterchangeBaseNode* Node = BaseNodeContainer->GetNode(Uid);

				if (UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(Node))
				{
					DisplayLabel = Node->GetDisplayLabel();
					NewNodeUid = Uid;
					return true;
				}

				if (UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(Node))
				{
					FString RefMeshUid;
					if (SceneNode->GetCustomMeshDependencyUid(RefMeshUid))
					{
						UInterchangeBaseNode* MeshNode = BaseNodeContainer->GetNode(RefMeshUid);
						if (MeshNode)
						{
							DisplayLabel = MeshNode->GetDisplayLabel();
							if (Uids.Num() == 1)
							{
								// If we are instancing one scene node, we want to use it to name the mesh
								DisplayLabel = SceneNode->GetDisplayLabel();
							}

							// Use the first scene node uid this LOD references, adding backslash since this uid is not asset typed (\\Mesh\\) like a mesh node
							// @TODO: change this so that scene nodes get a type prefix of their own?
							NewNodeUid = TEXT("\\") + Uid;
							return true;
						}
					}
				}
			}

			// We found the lod but there is no valid Mesh node to return the Uid
			break;
		}
	}

	return false;
}


UInterchangeSkeletalMeshFactoryNode* UInterchangeGenericAssetsPipeline::CreateSkeletalMeshFactoryNode(const FString& RootJointUid, const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex)
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
	
	// Create the skeletal mesh factory node, name it according to the first mesh node compositing the meshes
	FString SkeletalMeshUid_MeshNamePart;
	FString DisplayLabel;
	const int32 BaseLodIndex = 0;
	if (!MakeMeshFactoryNodeUidAndDisplayLabel(MeshUidsPerLodIndex, BaseLodIndex, SkeletalMeshUid_MeshNamePart, DisplayLabel))
	{
		// Log an error
		return nullptr;
	}

	const FString SkeletalMeshUid = TEXT("\\SkeletalMesh") + SkeletalMeshUid_MeshNamePart + SkeletonUid;
	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = NewObject<UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer, NAME_None);
	if (!ensure(SkeletalMeshFactoryNode))
	{
		return nullptr;
	}

	SkeletalMeshFactoryNode->InitializeSkeletalMeshNode(SkeletalMeshUid, DisplayLabel, USkeletalMesh::StaticClass()->GetName());
	SkeletalMeshFactoryNode->AddFactoryDependencyUid(SkeletonUid);
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
		if (!bImportLods && LodIndex > 0)
		{
			//If the pipeline should not import lods, skip any lod over base lod
			continue;
		}
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
				FString MeshDependency;
				SceneNode->GetCustomMeshDependencyUid(MeshDependency);
				if (BaseNodeContainer->IsNodeUidValid(MeshDependency))
				{
					SkeletalMeshFactoryNode->AddTargetAssetUid(MeshDependency);
				}

				SceneNode->GetMaterialDependencyUids(MaterialDependencies);
			}
			else if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				SkeletalMeshFactoryNode->AddTargetAssetUid(NodeUid);
				MeshNode->GetMaterialDependencies(MaterialDependencies);
			}
			const int32 MaterialCount = MaterialDependencies.Num();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				const FString MaterialFactoryNodeUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialDependencies[MaterialIndex]);
				if (BaseNodeContainer->IsNodeUidValid(MaterialFactoryNodeUid))
				{
					//Create a factory dependency so Material asset are import before the skeletal mesh asset
					TArray<FString> FactoryDependencies;
					SkeletalMeshFactoryNode->GetFactoryDependencies(FactoryDependencies);
					if (!FactoryDependencies.Contains(MaterialFactoryNodeUid))
					{
						SkeletalMeshFactoryNode->AddFactoryDependencyUid(MaterialFactoryNodeUid);
					}
				}
			}
			LodDataNode->AddMeshUid(NodeUid);
		}
	}
}


UInterchangeStaticMeshFactoryNode* UInterchangeGenericAssetsPipeline::CreateStaticMeshFactoryNode(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex)
{
	if (MeshUidsPerLodIndex.Num() == 0)
	{
		return nullptr;
	}

	// Create the static mesh factory node, name it according to the first mesh node compositing the meshes
	FString StaticMeshUid_MeshNamePart;
	FString DisplayLabel;
	const int32 BaseLodIndex = 0;
	if (!MakeMeshFactoryNodeUidAndDisplayLabel(MeshUidsPerLodIndex, BaseLodIndex, StaticMeshUid_MeshNamePart, DisplayLabel))
	{
		// Log an error
		return nullptr;
	}

	const FString StaticMeshUid = TEXT("\\StaticMesh") + StaticMeshUid_MeshNamePart;
	UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = NewObject<UInterchangeStaticMeshFactoryNode>(BaseNodeContainer, NAME_None);
	if (!ensure(StaticMeshFactoryNode))
	{
		return nullptr;
	}

	StaticMeshFactoryNode->InitializeStaticMeshNode(StaticMeshUid, DisplayLabel, UStaticMesh::StaticClass()->GetName());
	BaseNodeContainer->AddNode(StaticMeshFactoryNode);

	AddLodDataToStaticMesh(StaticMeshFactoryNode, MeshUidsPerLodIndex);

	return StaticMeshFactoryNode;
}


UInterchangeStaticMeshLodDataNode* UInterchangeGenericAssetsPipeline::CreateStaticMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID)
{
	FString DisplayLabel(NodeName);
	FString NodeUID(NodeUniqueID);
	UInterchangeStaticMeshLodDataNode* StaticMeshLodDataNode = NewObject<UInterchangeStaticMeshLodDataNode>(BaseNodeContainer, NAME_None);
	if (!ensure(StaticMeshLodDataNode))
	{
		// @TODO: log error
		return nullptr;
	}

	StaticMeshLodDataNode->InitializeNode(NodeUID, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_FactoryData);
	BaseNodeContainer->AddNode(StaticMeshLodDataNode);
	return StaticMeshLodDataNode;
}


void UInterchangeGenericAssetsPipeline::AddLodDataToStaticMesh(UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex)
{
	const FString StaticMeshFactoryUid = StaticMeshFactoryNode->GetUniqueID();

	for (const TPair<int32, TArray<FString>>& LodIndexAndNodeUids : NodeUidsPerLodIndex)
	{
		const int32 LodIndex = LodIndexAndNodeUids.Key;
		if (!bImportLods && LodIndex > 0)
		{
			// If the pipeline should not import lods, skip any lod over base lod
			continue;
		}

		const TArray<FString>& NodeUids = LodIndexAndNodeUids.Value;

		// Create a lod data node with all the meshes for this LOD
		const FString StaticMeshLodDataName = TEXT("LodData") + FString::FromInt(LodIndex);
		const FString LODDataPrefix = TEXT("\\LodData") + (LodIndex > 0 ? FString::FromInt(LodIndex) : TEXT(""));
		const FString StaticMeshLodDataUniqueID = LODDataPrefix + StaticMeshFactoryUid;

		// Create LodData node if it doesn't already exist
		UInterchangeStaticMeshLodDataNode* LodDataNode = Cast<UInterchangeStaticMeshLodDataNode>(BaseNodeContainer->GetNode(StaticMeshLodDataUniqueID));
		if (!LodDataNode)
		{
			// Add the data for the LOD (all the mesh node fbx path, so we can find them when we will create the payload data)
			LodDataNode = CreateStaticMeshLodDataNode(StaticMeshLodDataName, StaticMeshLodDataUniqueID);
			LodDataNode->SetParentUid(StaticMeshFactoryUid);
			StaticMeshFactoryNode->AddLodDataUniqueId(StaticMeshLodDataUniqueID);
		}

		for (const FString& NodeUid : NodeUids)
		{
			TArray<FString> MaterialDependencies;
			if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				FString MeshDependency;
				SceneNode->GetCustomMeshDependencyUid(MeshDependency);
				if (BaseNodeContainer->IsNodeUidValid(MeshDependency))
				{
					StaticMeshFactoryNode->AddTargetAssetUid(MeshDependency);
				}
				
				SceneNode->GetMaterialDependencyUids(MaterialDependencies);
			}
			else if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				StaticMeshFactoryNode->AddTargetAssetUid(NodeUid);
				MeshNode->GetMaterialDependencies(MaterialDependencies);
			}

			const int32 MaterialCount = MaterialDependencies.Num();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				const FString MaterialFactoryNodeUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialDependencies[MaterialIndex]);
				if (BaseNodeContainer->IsNodeUidValid(MaterialFactoryNodeUid))
				{
					// Create a factory dependency so Material asset are imported before the skeletal mesh asset
					TArray<FString> FactoryDependencies;
					StaticMeshFactoryNode->GetFactoryDependencies(FactoryDependencies);
					if (!FactoryDependencies.Contains(MaterialFactoryNodeUid))
					{
						StaticMeshFactoryNode->AddFactoryDependencyUid(MaterialFactoryNodeUid);
					}
				}
			}

			LodDataNode->AddMeshUid(NodeUid);
		}
	}
}
