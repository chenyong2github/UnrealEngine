// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "FbxHelper.h"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			struct FFbxSkelMesh
			{
			public:
				explicit FFbxSkelMesh(FFbxParser& InParser)
					: Parser(InParser)
				{}

				void FindSkeletalMeshes(FbxScene* SDKScene, TArray<TArray<FbxNode*>>& outSkelMeshArray, bool bCombineSkeletalMesh, bool bForceFindRigid);
				void FindAllLODGroupNode(TArray<FbxNode*>& OutNodeInLod, FbxNode* NodeLodGroup, int32 LodIndex);
				bool FindSkeletonJoints(FbxScene* SDKScene, TArray<FbxNode*>& NodeArray, TArray<FbxNode*>& SortedLinks, FbxArray<FbxAMatrix>& LocalsPerLink);
				FbxNode* FindLODGroupNode(FbxNode* NodeLodGroup, int32 LodIndex, FbxNode* NodeToFind);

				/**
				 * Add messages to the message log
				 */
				template <typename T>
				T* AddMessage(FbxGeometryBase* FbxNode) const
				{
					T* Item = Parser.AddMessage<T>();
					Item->MeshName = FFbxHelper::GetMeshName(FbxNode);
					Item->InterchangeKey = FFbxHelper::GetMeshUniqueID(FbxNode);
					return Item;
				}

			private:
				void RecursiveGetAllMeshNode(TArray<FbxNode*>& OutAllNode, FbxNode* Node);
				bool IsUnrealBone(FbxNode* Link);
				void RecursiveBuildSkeleton(FbxNode* Link, TArray<FbxNode*>& OutSortedLinks);
				bool RetrievePoseFromBindPose(FbxScene* SDKScene, const TArray<FbxNode*>& NodeArray, FbxArray<FbxPose*>& PoseArray);
				FbxNode* GetRootSkeleton(FbxScene* SDKScene, FbxNode* Link);
				void BuildSkeletonSystem(FbxScene* SDKScene, TArray<FbxCluster*>& ClusterArray, TArray<FbxNode*>& OutSortedLinks);
				FbxNode* RecursiveGetFirstMeshNode(FbxNode* Node, FbxNode* NodeToFind);
				void RecursiveFindFbxSkelMesh(FbxScene* SDKScene, FbxNode* Node, TArray< TArray<FbxNode*> >& outSkelMeshArray, TArray<FbxNode*>& SkeletonArray);
				void RecursiveFindRigidMesh(FbxScene* SDKScene, FbxNode* Node, TArray< TArray<FbxNode*> >& outSkelMeshArray, TArray<FbxNode*>& SkeletonArray);
				void RecursiveFixSkeleton(FbxScene* SDKScene, FbxNode* Node, TArray<FbxNode*>& SkelMeshes, bool bImportNestedMeshes);

				FFbxParser& Parser;
			};
		}//ns Private
	}//ns Interchange
}//ns UE
