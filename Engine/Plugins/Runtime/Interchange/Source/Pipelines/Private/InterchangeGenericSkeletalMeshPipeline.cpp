// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericAssetsPipeline.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "CoreMinimal.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePhysicsAssetFactoryNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangePipelineMeshesUtilities.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSourceData.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#if WITH_EDITOR
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#endif
#include "ReferenceSkeleton.h"
#include "Tasks/Task.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

namespace UE::Interchange::SkeletalMeshGenericPipeline
{
	FName SkeletalLodGetBoneName(const TArray<FMeshBoneInfo>& SkeletalLodRawInfos, int32 BoneIndex)
	{
		if (SkeletalLodRawInfos.IsValidIndex(BoneIndex))
		{
			return SkeletalLodRawInfos[BoneIndex].Name;
		}
		return NAME_None;
	}

	int32 SkeletalLodFindBoneIndex(const TArray<FMeshBoneInfo>& SkeletalLodRawInfos, FName BoneName)
	{
		const int32 BoneCount = SkeletalLodRawInfos.Num();
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			if (SkeletalLodRawInfos[BoneIndex].Name == BoneName)
			{
				return BoneIndex;
			}
		}
		return INDEX_NONE;
	}

	int32 SkeletalLodGetParentIndex(const TArray<FMeshBoneInfo>& SkeletalLodRawInfos, int32 BoneIndex)
	{
		if (SkeletalLodRawInfos.IsValidIndex(BoneIndex))
		{
			return SkeletalLodRawInfos[BoneIndex].ParentIndex;
		}
		return INDEX_NONE;
	}

	bool DoesParentChainMatch(int32 StartBoneIndex, const FReferenceSkeleton& SkeletonRef, const TArray<FMeshBoneInfo>& SkeletalLodRawInfos)
	{
		// if start is root bone
		if (StartBoneIndex == 0)
		{
			// verify name of root bone matches
			return (SkeletonRef.GetBoneName(0) == SkeletalLodGetBoneName(SkeletalLodRawInfos, 0));
		}

		int32 SkeletonBoneIndex = StartBoneIndex;
		// If skeleton bone is not found in mesh, fail.
		int32 MeshBoneIndex = SkeletalLodFindBoneIndex(SkeletalLodRawInfos, SkeletonRef.GetBoneName(SkeletonBoneIndex));
		if (MeshBoneIndex == INDEX_NONE)
		{
			return false;
		}
		do
		{
			// verify if parent name matches
			int32 ParentSkeletonBoneIndex = SkeletonRef.GetParentIndex(SkeletonBoneIndex);
			int32 ParentMeshBoneIndex = SkeletalLodGetParentIndex(SkeletalLodRawInfos, MeshBoneIndex);

			// if one of the parents doesn't exist, make sure both end. Otherwise fail.
			if ((ParentSkeletonBoneIndex == INDEX_NONE) || (ParentMeshBoneIndex == INDEX_NONE))
			{
				return (ParentSkeletonBoneIndex == ParentMeshBoneIndex);
			}

			// If parents are not named the same, fail.
			if (SkeletonRef.GetBoneName(ParentSkeletonBoneIndex) != SkeletalLodGetBoneName(SkeletalLodRawInfos, ParentMeshBoneIndex))
			{
				return false;
			}

			// move up
			SkeletonBoneIndex = ParentSkeletonBoneIndex;
			MeshBoneIndex = ParentMeshBoneIndex;
		} while (true);

		return true;
	}

	void RecursiveBuildSkeletalSkeleton(const FString JoinToAddUid, const int32 ParentIndex, const UInterchangeBaseNodeContainer* BaseNodeContainer, TArray<FMeshBoneInfo>& SkeletalLodRawInfos, TArray<FTransform>& SkeletalLodRawTransforms)
	{
		const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(JoinToAddUid));
		if (!SceneNode || !SceneNode->IsSpecializedTypeContains(FSceneNodeStaticData::GetJointSpecializeTypeString()))
		{
			return;
		}

		int32 JoinIndex = SkeletalLodRawInfos.Num();
		FMeshBoneInfo& Info = SkeletalLodRawInfos.AddZeroed_GetRef();
		Info.Name = *SceneNode->GetDisplayLabel();
		Info.ParentIndex = ParentIndex;
#if WITH_EDITORONLY_DATA
		Info.ExportName = Info.Name.ToString();
#endif
		FTransform& JoinTransform = SkeletalLodRawTransforms.AddZeroed_GetRef();
		SceneNode->GetCustomLocalTransform(JoinTransform);

		//Iterate childrens
		const TArray<FString> ChildrenIds = BaseNodeContainer->GetNodeChildrenUids(JoinToAddUid);
		for (int32 ChildIndex = 0; ChildIndex < ChildrenIds.Num(); ++ChildIndex)
		{
			RecursiveBuildSkeletalSkeleton(ChildrenIds[ChildIndex], JoinIndex, BaseNodeContainer, SkeletalLodRawInfos, SkeletalLodRawTransforms);
		}
	}

	bool IsCompatibleSkeleton(const USkeleton* Skeleton, const FString RootJoinUid, const UInterchangeBaseNodeContainer* BaseNodeContainer)
	{
		// at least % of bone should match 
		int32 NumOfBoneMatches = 0;
		//Make sure the specified Skeleton fit this skeletal mesh
		const FReferenceSkeleton& SkeletonRef = Skeleton->GetReferenceSkeleton();
		const int32 SkeletonBoneCount = SkeletonRef.GetRawBoneNum();

		TArray<FMeshBoneInfo> SkeletalLodRawInfos;
		SkeletalLodRawInfos.Reserve(SkeletonBoneCount);
		TArray<FTransform> SkeletalLodRawTransforms;
		SkeletalLodRawTransforms.Reserve(SkeletonBoneCount);
		RecursiveBuildSkeletalSkeleton(RootJoinUid, INDEX_NONE, BaseNodeContainer, SkeletalLodRawInfos, SkeletalLodRawTransforms);
		const int32 SkeletalLodBoneCount = SkeletalLodRawInfos.Num();

		// first ensure the parent exists for each bone
		for (int32 MeshBoneIndex = 0; MeshBoneIndex < SkeletalLodBoneCount; MeshBoneIndex++)
		{
			FName MeshBoneName = SkeletalLodRawInfos[MeshBoneIndex].Name;
			// See if Mesh bone exists in Skeleton.
			int32 SkeletonBoneIndex = SkeletonRef.FindBoneIndex(MeshBoneName);

			// if found, increase num of bone matches count
			if (SkeletonBoneIndex != INDEX_NONE)
			{
				++NumOfBoneMatches;

				// follow the parent chain to verify the chain is same
				if (!DoesParentChainMatch(SkeletonBoneIndex, SkeletonRef, SkeletalLodRawInfos))
				{
					//Not compatible
					return false;
				}
			}
			else
			{
				int32 CurrentBoneId = MeshBoneIndex;
				// if not look for parents that matches
				while (SkeletonBoneIndex == INDEX_NONE && CurrentBoneId != INDEX_NONE)
				{
					// find Parent one see exists
					const int32 ParentMeshBoneIndex = SkeletalLodGetParentIndex(SkeletalLodRawInfos, CurrentBoneId);
					if (ParentMeshBoneIndex != INDEX_NONE)
					{
						// @TODO: make sure RefSkeleton's root ParentIndex < 0 if not, I'll need to fix this by checking TreeBoneIdx
						FName ParentBoneName = SkeletalLodGetBoneName(SkeletalLodRawInfos, ParentMeshBoneIndex);
						SkeletonBoneIndex = SkeletonRef.FindBoneIndex(ParentBoneName);
					}

					// root is reached
					if (ParentMeshBoneIndex == 0)
					{
						break;
					}
					else
					{
						CurrentBoneId = ParentMeshBoneIndex;
					}
				}

				// still no match, return false, no parent to look for
				if (SkeletonBoneIndex == INDEX_NONE)
				{
					return false;
				}

				// second follow the parent chain to verify the chain is same
				if (!DoesParentChainMatch(SkeletonBoneIndex, SkeletonRef, SkeletalLodRawInfos))
				{
					return false;
				}
			}
		}

		// originally we made sure at least matches more than 50% 
		// but then slave components can't play since they're only partial
		// if the hierarchy matches, and if it's more then 1 bone, we allow
		return (NumOfBoneMatches > 0);
	}
}

bool UInterchangeGenericAssetsPipeline::ExecutePreImportPipelineSkeletalMesh()
{
	
	//PipelineMeshesUtilities;
	TArray<UInterchangeMeshNode*> SkinnedMeshNodes;
	TArray<UInterchangeMeshNode*> StaticMeshNodes;
	TMap<FString, TArray<FString>> SkeletalMeshFactoryDependencyOrderPerSkeletonRootNodeUid;
	
	//Find all translated node we need for this pipeline
	BaseNodeContainer->IterateNodes([this, &SkinnedMeshNodes, &StaticMeshNodes](const FString& NodeUid, UInterchangeBaseNode* Node)
	{
		switch(Node->GetNodeContainerType())
		{
			case EInterchangeNodeContainerType::NodeContainerType_TranslatedAsset:
			{
				if (UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(Node))
				{
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
		}
	});

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


	const UClass* SkeletalMeshFactoryNodeClass = UInterchangeSkeletalMeshFactoryNode::StaticClass();
	TArray<FString> SkeletalMeshNodeUids;
	BaseNodeContainer->GetNodes(SkeletalMeshFactoryNodeClass, SkeletalMeshNodeUids);

	//If we import only one asset, and bUseSourceNameForAsset is true, we want to rename the asset using the file name.
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

	//If we have a specified skeleton
	if (Skeleton)
	{
		SkeletonFactoryNode->SetEnabled(false);
		SkeletonFactoryNode->ReferenceObject = Skeleton;
	}
	return SkeletonFactoryNode;
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
	UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = Cast<UInterchangeSkeletonFactoryNode>(BaseNodeContainer->GetNode(SkeletonUid));
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
						if (SceneNode->GetCustomAssetInstanceUid(MeshNodeUid))
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
	SkeletalMeshFactoryNode->AddFactoryDependencyUid(SkeletonUid);
	BaseNodeContainer->AddNode(SkeletalMeshFactoryNode);

	AddLodDataToSkeletalMesh(SkeletonFactoryNode, SkeletalMeshFactoryNode, MeshUidsPerLodIndex);
	SkeletalMeshFactoryNode->SetCustomImportMorphTarget(bImportMorphTargets);

	//If we have a specified skeleton
	if (Skeleton)
	{
		if (UE::Interchange::SkeletalMeshGenericPipeline::IsCompatibleSkeleton(Skeleton, RootJointNode->GetUniqueID(), BaseNodeContainer))
		{
			FSoftObjectPath SkeletonSoftObjectPath(Skeleton.Get());
			SkeletalMeshFactoryNode->SetCustomSkeletonSoftObjectPath(SkeletonSoftObjectPath);
		}
		else
		{
			//Log an error, saying we will create a skeleton instead of using the specified one
			//Should we show a dialog so we are sure the user understand he choose the wrong skeleton

			//Make sure we enable the skeleton factory node
			SkeletonFactoryNode->SetEnabled(true);

		}
	}

#if WITH_EDITOR
	//Physic asset dependency, if we must create or use a specialize physic asset let create
	//a PhysicsAsset factory node, so the asset will exist when we will setup the skeletalmesh
	if (bCreatePhysicsAsset)
	{
		UInterchangePhysicsAssetFactoryNode* PhysicsAssetFactoryNode = NewObject<UInterchangePhysicsAssetFactoryNode>(BaseNodeContainer, NAME_None);
		if (ensure(SkeletalMeshFactoryNode))
		{
			const FString PhysicsAssetUid = TEXT("\\PhysicsAsset") + SkeletalMeshUid_MeshNamePart + SkeletonUid;
			const FString PhysicsAssetDisplayLabel = DisplayLabel + TEXT("_PhysicsAsset");
			PhysicsAssetFactoryNode->InitializePhysicsAssetNode(PhysicsAssetUid, PhysicsAssetDisplayLabel, UPhysicsAsset::StaticClass()->GetName());
			PhysicsAssetFactoryNode->SetCustomSkeletalMeshUid(SkeletalMeshUid);
			BaseNodeContainer->AddNode(PhysicsAssetFactoryNode);
		}
	}
	SkeletalMeshFactoryNode->SetCustomCreatePhysicsAsset(bCreatePhysicsAsset);
	if (!bCreatePhysicsAsset && !PhysicsAsset.IsNull())
	{
		FSoftObjectPath PhysicSoftObjectPath(PhysicsAsset.Get());
		SkeletalMeshFactoryNode->SetCustomPhysicAssetSoftObjectPath(PhysicSoftObjectPath);
	}
#endif

	const bool bTrueValue = true;
	switch (VertexColorImportOption)
	{
		case EInterchangeVertexColorImportOption::IVCIO_Replace:
		{
			SkeletalMeshFactoryNode->SetCustomVertexColorReplace(bTrueValue);
		}
		break;
		case EInterchangeVertexColorImportOption::IVCIO_Ignore:
		{
			SkeletalMeshFactoryNode->SetCustomVertexColorIgnore(bTrueValue);
		}
		break;
		case EInterchangeVertexColorImportOption::IVCIO_Override:
		{
			SkeletalMeshFactoryNode->SetCustomVertexColorOverride(VertexOverrideColor);
		}
		break;
	}

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
				SceneNode->GetCustomAssetInstanceUid(MeshDependency);
				if (BaseNodeContainer->IsNodeUidValid(MeshDependency))
				{
					SkeletalMeshFactoryNode->AddTargetNodeUid(MeshDependency);
					BaseNodeContainer->GetNode(MeshDependency)->AddTargetNodeUid(SkeletalMeshFactoryNode->GetUniqueID());
				}

				SceneNode->GetMaterialDependencyUids(MaterialDependencies);
			}
			else if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				SkeletalMeshFactoryNode->AddTargetNodeUid(NodeUid);
				MeshNode->AddTargetNodeUid(SkeletalMeshFactoryNode->GetUniqueID());
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

void UInterchangeGenericAssetsPipeline::PostImportPhysicsAssetImport(UObject* CreatedAsset, UInterchangeBaseNode* Node)
{
#if WITH_EDITOR
	if (!bCreatePhysicsAsset || !BaseNodeContainer)
	{
		return;
	}

	UPhysicsAsset* CreatedPhysicsAsset = Cast<UPhysicsAsset>(CreatedAsset);
	if (!CreatedPhysicsAsset)
	{
		return;
	}
	if (UInterchangePhysicsAssetFactoryNode* PhysicsAssetFactoryNode = Cast<UInterchangePhysicsAssetFactoryNode>(Node))
	{
		FString SkeletalMeshFactoryNodeUid;
		if (PhysicsAssetFactoryNode->GetCustomSkeletalMeshUid(SkeletalMeshFactoryNodeUid))
		{
			if (UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer->GetNode(SkeletalMeshFactoryNodeUid)))
			{
				if (SkeletalMeshFactoryNode->ReferenceObject.IsValid())
				{
					if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SkeletalMeshFactoryNode->ReferenceObject.TryLoad()))
					{
						auto CreateFromSkeletalMeshLambda = [CreatedPhysicsAsset, SkeletalMesh]()
						{
							FPhysAssetCreateParams NewBodyData;
							FText CreationErrorMessage;
							if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(CreatedPhysicsAsset, SkeletalMesh, NewBodyData, CreationErrorMessage))
							{
								//TODO: Log an error
							}
						};

						if (!IsInGameThread() && SkeletalMesh->IsCompiling())
						{
							//If the skeletalmesh is compiling we have to stall on the main thread
							Async(EAsyncExecution::TaskGraphMainThread, [CreateFromSkeletalMeshLambda]()
							{
								CreateFromSkeletalMeshLambda();
							});
						}
						else
						{
							CreateFromSkeletalMeshLambda();
						}
					}
				}
			}
		}
	}
#endif //WITH_EDITOR
}

void UInterchangeGenericAssetsPipeline::ImplementUseSourceNameForAssetOptionSkeletalMesh(const int32 MeshesAndAnimsImportedNodeCount)
{
	const UClass* SkeletalMeshFactoryNodeClass = UInterchangeSkeletalMeshFactoryNode::StaticClass();
	TArray<FString> SkeletalMeshNodeUids;
	BaseNodeContainer->GetNodes(SkeletalMeshFactoryNodeClass, SkeletalMeshNodeUids);
	if (SkeletalMeshNodeUids.Num() == 0)
	{
		return;
	}
	//If we import only one asset, and bUseSourceNameForAsset is true, we want to rename the asset using the file name.
	const bool bShouldChangeAssetName = (bUseSourceNameForAsset && MeshesAndAnimsImportedNodeCount == 1);
	const FString SkeletalMeshUid = SkeletalMeshNodeUids[0];
	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshNode = Cast<UInterchangeSkeletalMeshFactoryNode>(BaseNodeContainer->GetNode(SkeletalMeshUid));
	if (!SkeletalMeshNode)
	{
		return;
	}

	FString DisplayLabelName = SkeletalMeshNode->GetDisplayLabel();
		
	if (bShouldChangeAssetName)
	{
		DisplayLabelName = FPaths::GetBaseFilename(SourceDatas[0]->GetFilename());
		SkeletalMeshNode->SetDisplayLabel(DisplayLabelName);
	}

	//Also set the skeleton factory node name
	TArray<FString> LodDataUids;
	SkeletalMeshNode->GetLodDataUniqueIds(LodDataUids);
	if (LodDataUids.Num() > 0)
	{
		//Get the skeleton from the base LOD, skeleton is shared with all LODs
		if (UInterchangeSkeletalMeshLodDataNode* SkeletalMeshLodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(BaseNodeContainer->GetNode(LodDataUids[0])))
		{
			//If the user did not specify any skeleton
			if (Skeleton.IsNull())
			{
				FString SkeletalMeshSkeletonUid;
				SkeletalMeshLodDataNode->GetCustomSkeletonUid(SkeletalMeshSkeletonUid);
				UInterchangeSkeletonFactoryNode* SkeletonFactoryNode = Cast<UInterchangeSkeletonFactoryNode>(BaseNodeContainer->GetNode(SkeletalMeshSkeletonUid));
				if (SkeletonFactoryNode)
				{
					const FString SkeletonName = DisplayLabelName + TEXT("_Skeleton");
					SkeletonFactoryNode->SetDisplayLabel(SkeletonName);
				}
			}
		}
	}
	const UClass* PhysicsAssetFactoryNodeClass = UInterchangePhysicsAssetFactoryNode::StaticClass();
	TArray<FString> PhysicsAssetNodeUids;
	BaseNodeContainer->GetNodes(PhysicsAssetFactoryNodeClass, PhysicsAssetNodeUids);
	for (const FString& PhysicsAssetNodeUid : PhysicsAssetNodeUids)
	{
		UInterchangePhysicsAssetFactoryNode* PhysicsAssetFactoryNode = Cast<UInterchangePhysicsAssetFactoryNode>(BaseNodeContainer->GetNode(PhysicsAssetNodeUid));
		if (!ensure(PhysicsAssetFactoryNode))
		{
			continue;
		}
		FString PhysicsAssetSkeletalMeshUid;
		if (PhysicsAssetFactoryNode->GetCustomSkeletalMeshUid(PhysicsAssetSkeletalMeshUid) && PhysicsAssetSkeletalMeshUid.Equals(SkeletalMeshUid))
		{
			//Rename this asset
			const FString PhysicsAssetName = DisplayLabelName + TEXT("_PhysicsAsset");
			PhysicsAssetFactoryNode->SetDisplayLabel(PhysicsAssetName);
		}
	}
}