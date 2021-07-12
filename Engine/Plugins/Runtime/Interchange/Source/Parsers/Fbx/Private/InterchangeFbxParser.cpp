// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeFbxParser.h"

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeTextureNode.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "InterchangeFbxParser"

namespace UE
{
	namespace Interchange
	{
		FInterchangeFbxParser::FInterchangeFbxParser()
		{
			ResultsContainer = NewObject<UInterchangeResultsContainer>(GetTransientPackage());
			ResultsContainer->AddToRoot();
			FbxParserPrivate = MakeUnique<Private::FFbxParser>(ResultsContainer);
		}

		FInterchangeFbxParser::~FInterchangeFbxParser()
		{
			ResultsContainer->RemoveFromRoot();
			FbxParserPrivate = nullptr;
		}

		void FInterchangeFbxParser::LoadFbxFile(const FString& Filename, const FString& ResultFolder)
		{
			check(FbxParserPrivate.IsValid());
			SourceFilename = Filename;
			ResultsContainer->Empty();

			if (!FbxParserPrivate->LoadFbxFile(Filename))
			{
				UInterchangeResultError_Generic* Error = AddMessage<UInterchangeResultError_Generic>();
				Error->SourceAssetName = SourceFilename;
				Error->Text = LOCTEXT("CantLoadFbxFile", "Cannot load the FBX file.");
				return;
			}

			ResultFilepath = ResultFolder + TEXT("/SceneDescription.itc");
			//Since we are not in main thread we cannot use TStrongPtr, so we will add the object to the root and remove it when we are done
			UInterchangeBaseNodeContainer* Container = NewObject<UInterchangeBaseNodeContainer>(GetTransientPackage(), NAME_None);
			if (!ensure(Container != nullptr))
			{
				UInterchangeResultError_Generic* Error = AddMessage<UInterchangeResultError_Generic>();
				Error->SourceAssetName = SourceFilename;
				Error->Text = LOCTEXT("CantAllocate", "Cannot allocate base node container to add FBX scene data.");
				return;
			}

			Container->AddToRoot();
			FbxParserPrivate->FillContainerWithFbxScene(*Container);
			Container->SaveToFile(ResultFilepath);
			Container->RemoveFromRoot();
		}

		void FInterchangeFbxParser::FetchPayload(const FString& PayloadKey, const FString& ResultFolder)
		{
			check(FbxParserPrivate.IsValid());
			ResultsContainer->Empty();

			FString& PayloadFilepath = ResultPayloads.FindOrAdd(PayloadKey);
			PayloadFilepath = ResultFolder + TEXT("/") + PayloadKey + TEXT(".payload");

			//Copy the map filename key because we are multithreaded and the TMap can be reallocated
			FString PayloadFilepathCopy = PayloadFilepath;
			if (!FbxParserPrivate->FetchPayloadData(PayloadKey, PayloadFilepathCopy))
			{
				UInterchangeResultError_Generic* Error = AddMessage<UInterchangeResultError_Generic>();
				Error->SourceAssetName = SourceFilename;
				Error->Text = LOCTEXT("CantFetchPayload", "Cannot fetch FBX payload data.");
				return;
			}
		}

		TArray<FString> FInterchangeFbxParser::GetJsonLoadMessages() const
		{
			TArray<FString> JsonResults;
			for (UInterchangeResult* Result : ResultsContainer->GetResults())
			{
				JsonResults.Add(Result->ToJson());
			}

			return JsonResults;
		}

	}//ns Interchange
}//ns UE

#undef LOCTEXT_NAMESPACE
