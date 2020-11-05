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

			Container = TStrongObjectPtr<UInterchangeBaseNodeContainer>(NewObject<UInterchangeBaseNodeContainer>(GetTransientPackage(), NAME_None));
			if (!ensure(Container.IsValid()))
			{
				JsonLoadMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate base node container to add fbx scene data\"}}"));
				return;
			}
			FbxParserPrivate->FillContainerWithFbxScene(*Container.Get(), JsonLoadMessages);

			Container.Get()->SaveToFile(ResultFilepath);
			JsonLoadMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Log\",\n\"Msg\" : \"This is a success!\"}}"));
		}
	}//ns Interchange
}//ns UE
