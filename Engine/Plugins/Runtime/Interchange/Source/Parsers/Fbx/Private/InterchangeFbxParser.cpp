// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeFbxParser.h"

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeTextureNode.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

namespace UE
{
	namespace Interchange
	{
		FInterchangeFbxParser::FInterchangeFbxParser()
		{
			FbxParserPrivate = MakeUnique<Private::FFbxParser>();
		}
		FInterchangeFbxParser::~FInterchangeFbxParser()
		{
			FbxParserPrivate = nullptr;
		}

		void FInterchangeFbxParser::LoadFbxFile(const FString& Filename, const FString& ResultFolder)
		{
			check(FbxParserPrivate.IsValid());

			if (!FbxParserPrivate->LoadFbxFile(Filename, JsonLoadMessages))
			{
				JsonLoadMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot load the fbx file.\"}}"));
				return;
			}


			ResultFilepath = ResultFolder + TEXT("/SceneDescription.itc");
			//Since we are not in main thread we cannot use TStrongPtr, so we will add the object to the root and remove it when we are done
			UInterchangeBaseNodeContainer* Container = NewObject<UInterchangeBaseNodeContainer>(GetTransientPackage(), NAME_None);
			Container->AddToRoot();
			if (!ensure(Container != nullptr))
			{
				JsonLoadMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate base node container to add fbx scene data\"}}"));
				return;
			}
			FbxParserPrivate->FillContainerWithFbxScene(*Container, JsonLoadMessages);

			Container->SaveToFile(ResultFilepath);
			Container->RemoveFromRoot();
			JsonLoadMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Log\",\n\"Msg\" : \"This is a success!\"}}"));
		}

		void FInterchangeFbxParser::FetchPayload(const FString& PayloadKey, const FString& ResultFolder)
		{
			check(FbxParserPrivate.IsValid());

			FString& PayloadFilepath = ResultPayloads.FindOrAdd(PayloadKey);
			PayloadFilepath = ResultFolder + TEXT("/") + PayloadKey + TEXT(".payload");
			//Copy the map filename key because we are multithreaded and the TMap can be reallocated
			FString PayloadFilepathCopy = PayloadFilepath;
			if (!FbxParserPrivate->FetchPayloadData(PayloadKey, PayloadFilepathCopy, JsonLoadMessages))
			{
				JsonLoadMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot fetch fbx payload data.\"}}"));
				return;
			}
			
			JsonLoadMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Log\",\n\"Msg\" : \"Fetch fbx Payload success!\"}}"));
		}
	}//ns Interchange
}//ns UE
