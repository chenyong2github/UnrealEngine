// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

#define FBX_METADATA_PREFIX TEXT("FBX.")
#define INVALID_UNIQUE_ID 0xFFFFFFFFFFFFFFFF

class UInterchangeBaseNodeContainer;

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FFbxParser
			{
			public:
				/* Load an fbx file into the fbx sdk, return false if the file could not be load. */
				bool LoadFbxFile(const FString& Filename, TArray<FString>& JsonErrorMessages);

				/* Extract the fbx data from the sdk into our node container */
				void FillContainerWithFbxScene(UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JsonErrorMessages);
			
			private:
				FbxManager* SDKManager = nullptr;
				FbxScene* SDKScene = nullptr;
				FbxImporter* SDKImporter = nullptr;
				//FbxGeometryConverter* SDKGeometryConverter = nullptr;
			};
		}//ns Private
	}//ns Interchange
}//ns UE
