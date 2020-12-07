// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FPayloadContextBase
			{
			public:
				virtual ~FPayloadContextBase() {}
				virtual FString GetPayloadType() const { return FString(); }
				virtual bool FetchPayloadToFile(const FString& PayloadFilepath, TArray<FString>& JSonErrorMessages) { return false; }
			};

			struct FFbxHelper
			{
			public:

				/**
				 * Return the name of an FbxObject, return empty string if the object is null.
				 */
				static FString GetFbxObjectName(const FbxObject* Object);

				/**
				 * Return a string with the name of all the parent in the hierarchy separate by a dot( . ) from the fbx root node to the specified node.
				 * This is a way to have a valid unique ID for a fbx node that will be the same if the fbx change when we re-import.
				 * Using the fbx sdk int32 uniqueID is not valid anymore if the fbx is re-exported.
				 */
				static FString GetFbxNodeHierarchyName(const FbxNode* Node);

				//Skeletalmesh helper
				static void FindSkeletalMeshes(FbxScene* SDKScene, TArray< TArray<FbxNode*> >& outSkelMeshArray, bool bCombineSkeletalMesh, bool bForceFindRigid);
				static void FindAllLODGroupNode(TArray<FbxNode*>& OutNodeInLod, FbxNode* NodeLodGroup, int32 LodIndex);
				static bool FindSkeletonJoints(FbxScene* SDKScene, TArray<FbxNode*>& NodeArray, TArray<FbxNode*>& SortedLinks, FbxArray<FbxAMatrix>& LocalsPerLink);
				static FbxNode* FindLODGroupNode(FbxNode* NodeLodGroup, int32 LodIndex, FbxNode* NodeToFind);

			private:

				/* Skletalmesh private helper begin */

				static void RecursiveGetAllMeshNode(TArray<FbxNode*>& OutAllNode, FbxNode* Node);
				static bool IsUnrealBone(FbxNode* Link);
				static void RecursiveBuildSkeleton(FbxNode* Link, TArray<FbxNode*>& OutSortedLinks);
				static bool RetrievePoseFromBindPose(FbxScene* SDKScene, const TArray<FbxNode*>& NodeArray, FbxArray<FbxPose*>& PoseArray);
				static FbxNode* GetRootSkeleton(FbxScene* SDKScene, FbxNode* Link);
				static void BuildSkeletonSystem(FbxScene* SDKScene, TArray<FbxCluster*>& ClusterArray, TArray<FbxNode*>& OutSortedLinks);
				static FbxNode* RecursiveGetFirstMeshNode(FbxNode* Node, FbxNode* NodeToFind);
				static void RecursiveFindFbxSkelMesh(FbxScene* SDKScene, FbxNode* Node, TArray< TArray<FbxNode*> >& outSkelMeshArray, TArray<FbxNode*>& SkeletonArray);
				static void RecursiveFindRigidMesh(FbxScene* SDKScene, FbxNode* Node, TArray< TArray<FbxNode*> >& outSkelMeshArray, TArray<FbxNode*>& SkeletonArray);
				static void RecursiveFixSkeleton(FbxScene* SDKScene, FbxNode* Node, TArray<FbxNode*>& SkelMeshes, bool bImportNestedMeshes);

				/* Skletalmesh private helper end */
			};
		}//ns Private
	}//ns Interchange
}//ns UE
