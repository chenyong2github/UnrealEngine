// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxHelper.h"
#include "FbxInclude.h"

/** Forward declarations */
class FSkeletalMeshAttributes;
class UInterchangeBaseNode;
class UInterchangeBaseNodeContainer;
class UInterchangeSkeletalMeshNode;
class UInterchangeSkeletalMeshLodDataNode;

struct FMeshDescription;


namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FSkeletalMeshGeometryPayload : public FPayloadContextBase
			{
			public:
				virtual ~FSkeletalMeshGeometryPayload() { SkelMeshNodeArray.Empty(); }
				virtual FString GetPayloadType() const override { return TEXT("SkeletalMesh-GeometryPayload"); }
				virtual bool FetchPayloadToFile(const FString& PayloadFilepath, TArray<FString>& JSonErrorMessages) override;
				TArray<FbxNode*> SkelMeshNodeArray;
				TArray<FbxNode*> SortedJoints;
				FbxScene* SDKScene = nullptr;
				FbxGeometryConverter* SDKGeometryConverter = nullptr;

			private:
				bool AddFbxMeshToMeshDescription(int32 MeshIndex, FMeshDescription& SkeletalMeshDescription, FSkeletalMeshAttributes& SkeletalMeshAttribute, TArray<FString>& JSonErrorMessages);
			};

			class FFbxSkeletalMesh
			{
			public:
				static void FindSkeletalMeshes(FbxScene* Scene, TArray< TArray<FbxNode*> >& outSkelMeshArray, bool bCombineSkeletalMesh, bool bForceFindRigid);
				static void AddAllSceneSkeletalMeshes(FbxScene* SDKScene, FbxGeometryConverter* SDKGeometryConverter, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages, TMap<FString, TSharedPtr<FPayloadContextBase>>& PayloadContexts, const FString& SourceFilename);
				static UInterchangeSkeletalMeshNode* CreateSkeletalMeshNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID, TArray<FString>& JSonErrorMessages);
				static UInterchangeSkeletalMeshLodDataNode* CreateSkeletalMeshLodDataNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID, TArray<FString>& JSonErrorMessages);
			
			private:
			};
		}//ns Private
	}//ns Interchange
}//ns UE
