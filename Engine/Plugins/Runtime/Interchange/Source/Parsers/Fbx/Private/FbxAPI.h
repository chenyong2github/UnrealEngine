// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"

#define FBX_METADATA_PREFIX TEXT("FBX.")
#define INVALID_UNIQUE_ID 0xFFFFFFFFFFFFFFFF

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FPayloadContextBase;
		}
	}
}
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
				~FFbxParser();

				/* Load an fbx file into the fbx sdk, return false if the file could not be load. */
				bool LoadFbxFile(const FString& Filename, TArray<FString>& JsonErrorMessages);

				/* Extract the fbx data from the sdk into our node container */
				void FillContainerWithFbxScene(UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JsonErrorMessages);

				/* Extract the fbx data from the sdk into our node container */
				bool FetchPayloadData(const FString& PayloadKey, const FString& PayloadFilepath, TArray<FString>& JSonErrorMessages);
			
			private:
				FbxManager* SDKManager = nullptr;
				FbxScene* SDKScene = nullptr;
				FbxImporter* SDKImporter = nullptr;
				FbxGeometryConverter* SDKGeometryConverter = nullptr;
				FString SourceFilename;

				TMap<FString, TSharedPtr<FPayloadContextBase>> PayloadContexts;
				
			};
		}//ns Private
	}//ns Interchange
}//ns UE
