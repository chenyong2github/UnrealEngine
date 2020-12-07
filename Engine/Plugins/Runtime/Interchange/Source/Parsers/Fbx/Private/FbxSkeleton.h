// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

/** Forward declarations */
class UInterchangeBaseNode;
class UInterchangeBaseNodeContainer;
class UInterchangeJointNode;
class UInterchangeSkeletonNode;

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FFbxSkeleton
			{
			public:
				static void AddAllSceneSkeletons(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages, const FString& SourceFilename);
				static UInterchangeSkeletonNode* CreateSkeletonNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& UniqueID, TArray<FString>& JSonErrorMessages);
				static UInterchangeJointNode* CreateJointNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& UniqueID, TArray<FString>& JSonErrorMessages);

			private:
			};
		}//ns Private
	}//ns Interchange
}//ns UE
