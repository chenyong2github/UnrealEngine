// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxSkeleton.h"

#include "CoreMinimal.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "FbxSkeletalMesh.h"
#include "InterchangeJointNode.h"
#include "InterchangeSkeletonNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#define GeneratedLODNameSuffix "_GeneratedLOD_"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			void FFbxSkeleton::AddAllSceneSkeletons(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages, const FString& SourceFilename)
			{
				TArray<TArray<FbxNode*>> SkeletalMeshes;
				const bool bCombineSkeletalMesh = false;
				const bool bForceFindRigid = false;
				FFbxHelper::FindSkeletalMeshes(SDKScene, SkeletalMeshes, bCombineSkeletalMesh, bForceFindRigid);
				int32 SkeletalMeshCount = SkeletalMeshes.Num();
				for (int32 SkeletalMeshIndex = 0; SkeletalMeshIndex < SkeletalMeshCount; ++SkeletalMeshIndex)
				{
					TArray<FbxNode*>& NodeArray = SkeletalMeshes[SkeletalMeshIndex];
					int32 MaxNumberOfLOD = 1;
					for (int32 NodeIndex = 0; NodeIndex < NodeArray.Num(); NodeIndex++)
					{
						FbxNode* Node = NodeArray[NodeIndex];
						if (Node->GetNodeAttribute() && Node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eLODGroup)
						{
							// get max LODgroup level
							if (MaxNumberOfLOD < Node->GetChildCount())
							{
								MaxNumberOfLOD = Node->GetChildCount();
							}
						}
					}

					bool bOverrideSkeletalMeshName = false;
					FString OverrideSkeletalMeshName;
					if (SkeletalMeshCount == 1)
					{
						bOverrideSkeletalMeshName = true;
						OverrideSkeletalMeshName = FPaths::GetBaseFilename(SourceFilename);
					}

					for (int32 LODIndex = 0; LODIndex < MaxNumberOfLOD; LODIndex++)
					{
						TArray<FbxNode*> SkelMeshNodeArray;
						for (int32 j = 0; j < NodeArray.Num(); j++)
						{
							FbxNode* Node = NodeArray[j];
							if (Node->GetNodeAttribute() && Node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eLODGroup)
							{
								TArray<FbxNode*> NodeInLod;
								if (Node->GetChildCount() > LODIndex)
								{
									FFbxHelper::FindAllLODGroupNode(NodeInLod, Node, LODIndex);
								}
								else // in less some LODGroups have less level, use the last level
								{
									FFbxHelper::FindAllLODGroupNode(NodeInLod, Node, Node->GetChildCount() - 1);
								}

								for (FbxNode* MeshNode : NodeInLod)
								{
									SkelMeshNodeArray.Add(MeshNode);
								}
							}
							else
							{
								SkelMeshNodeArray.Add(Node);
							}
						}
						if (SkelMeshNodeArray.Num() <= 0)
						{
							//Nothing to import for this LOD
							continue;
						}
						//Find the skeleton for this skeletal mesh LOD
						TArray<FbxNode*> SortedJoints;
						FbxArray<FbxAMatrix> LocalsPerLink;
						if (!FFbxHelper::FindSkeletonJoints(SDKScene, SkelMeshNodeArray, SortedJoints, LocalsPerLink))
						{
							continue;
						}
						//A skeleton must have at least one joint
						if (SortedJoints.Num() <= 0)
						{
							continue;
						}
						FString SkeletonName = (bOverrideSkeletalMeshName ? OverrideSkeletalMeshName : FFbxHelper::GetFbxObjectName(SkelMeshNodeArray[0])) + TEXT("_Skeleton");
						if(LODIndex > 0)
						{
							SkeletonName += TEXT("_") + FString::FromInt(LODIndex);
						}

						FString SkeletonUniqueID = TEXT("\\Skeleton\\") + FFbxHelper::GetFbxNodeHierarchyName(SortedJoints[0]);

						UInterchangeBaseNode* ExistingNode = NodeContainer.GetNode(*SkeletonUniqueID);
						if (ExistingNode)
						{
							//The skeleton already exist
							UInterchangeSkeletonNode* ExistingSkeletonNode = Cast<UInterchangeSkeletonNode>(ExistingNode);
							//It must be of the correct type
							if (!ensure(ExistingSkeletonNode))
							{
								JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot parse properly a fbx skeleton\"}}"));
							}
							//Skip this LOD since the skeleton is already created
							continue;
						}

						UInterchangeSkeletonNode* SkeletonNode = CreateSkeletonNode(NodeContainer, SkeletonName, SkeletonUniqueID, JSonErrorMessages);
						if (!ensure(SkeletonNode))
						{
							continue;
						}

						for (int32 JointIndex = 0; JointIndex < SortedJoints.Num(); ++JointIndex)
						{
							if (!ensure(LocalsPerLink.GetCount() > JointIndex))
							{
								JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot parse properly a fbx skeleton\"}}"));
								break;
							}
							FbxNode* FbxJointNode = SortedJoints[JointIndex];
							//Create an interchange joint node
							FString JointName = FFbxHelper::GetFbxObjectName(FbxJointNode);
							FString JointUniqueID = TEXT("\\Joint\\") + FFbxHelper::GetFbxNodeHierarchyName(FbxJointNode);
							UInterchangeJointNode* JointNode = CreateJointNode(NodeContainer, JointName, JointUniqueID, JSonErrorMessages);
							if (!ensure(JointNode))
							{
								break;
							}
							JointNode->SetCustomName(*JointName);
							JointNode->SetCustomLocalTransform(FFbxConvert::ConvertTransform(LocalsPerLink[JointIndex]));
							if (JointIndex == 0)
							{
								SkeletonNode->SetCustomRootJointID(*JointUniqueID);
							}
							else
							{
								const FbxNode* FbxParentJoint = FbxJointNode->GetParent();
								FString ParentJointUniqueID = TEXT("\\Joint\\") + FFbxHelper::GetFbxNodeHierarchyName(FbxParentJoint);
								JointNode->SetParentUID(*ParentJointUniqueID);
							}
						}
						FName SkeletonRootJointIDValidator;
						if (SkeletonNode->GetCustomRootJointID(SkeletonRootJointIDValidator))
						{
							UInterchangeBaseNode* RootNode = NodeContainer.GetNode(SkeletonRootJointIDValidator);
							FName RootNodeDisplayLabel = RootNode->GetDisplayLabel();
						}
					}
				}
			}

			UInterchangeSkeletonNode* FFbxSkeleton::CreateSkeletonNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& UniqueID, TArray<FString>& JSonErrorMessages)
			{
				FName DisplayLabel = *NodeName;
				FName NodeUID(*UniqueID);

				UInterchangeBaseNode* ExistingNode = NodeContainer.GetNode(NodeUID);
				if (ExistingNode)
				{
					UInterchangeSkeletonNode* ExistingSkeletonNode = Cast<UInterchangeSkeletonNode>(ExistingNode);
					return (ensure(ExistingSkeletonNode) ? ExistingSkeletonNode : nullptr);
				}

				UInterchangeSkeletonNode* SkeletonNode = NewObject<UInterchangeSkeletonNode>(&NodeContainer, NAME_None);
				if (!ensure(SkeletonNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a skeleton node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UMaterialInterface
				SkeletonNode->InitializeSkeletonNode(NodeUID, DisplayLabel, TEXT("Skeleton"));
				NodeContainer.AddNode(SkeletonNode);
				return SkeletonNode;
			}

			UInterchangeJointNode* FFbxSkeleton::CreateJointNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& UniqueID, TArray<FString>& JSonErrorMessages)
			{
				FName DisplayLabel = *NodeName;
				FName NodeUID(*UniqueID);
				UInterchangeJointNode* JointNode = NewObject<UInterchangeJointNode>(&NodeContainer, NAME_None);
				if (!ensure(JointNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a joint node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UMaterialInterface
				JointNode->InitializeNode(NodeUID, DisplayLabel);
				NodeContainer.AddNode(JointNode);
				return JointNode;
			}

		} //ns Private
	} //ns Interchange
}//ns UE
