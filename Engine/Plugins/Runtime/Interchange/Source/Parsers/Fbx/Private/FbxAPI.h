// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"
#include "InterchangeResultsContainer.h"


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
				explicit FFbxParser(UInterchangeResultsContainer* InResultsContainer)
					: ResultsContainer(InResultsContainer)
				{}

				~FFbxParser();

				/* Load an fbx file into the fbx sdk, return false if the file could not be load. */
				bool LoadFbxFile(const FString& Filename);

				/* Extract the fbx data from the sdk into our node container */
				void FillContainerWithFbxScene(UInterchangeBaseNodeContainer& NodeContainer);

				/* Extract the fbx data from the sdk into our node container */
				bool FetchPayloadData(const FString& PayloadKey, const FString& PayloadFilepath);
			
				/**
				 * This function is used to add the given message object directly into the results for this operation.
				 */
				template <typename T>
				T* AddMessage() const
				{
					check(ResultsContainer != nullptr);
					T* Item = ResultsContainer->Add<T>();
					Item->SourceAssetName = SourceFilename;
					return Item;
				}


				void AddMessage(UInterchangeResult* Item) const
				{
					check(ResultsContainer != nullptr);
					ResultsContainer->Add(Item);
					Item->SourceAssetName = SourceFilename;
				}
			private:

				void CleanupFbxData();

				UInterchangeResultsContainer* ResultsContainer;
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
