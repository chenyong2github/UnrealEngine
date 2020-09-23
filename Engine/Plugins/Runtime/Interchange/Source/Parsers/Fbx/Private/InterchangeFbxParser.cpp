// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeFbxParser.h"

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeTextureNode.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

namespace InterchangeFbxParser
{
	void FbxParser::LoadFbxFile(const FString& Filename, const FString& ResultFolder)
	{
		UE::FbxParser::Private::FFbxParser FbxParserPrivate;
		if (!FbxParserPrivate.LoadFbxFile(Filename, JsonLoadMessages))
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
		FbxParserPrivate.FillContainerWithFbxScene(*Container.Get(), JsonLoadMessages);
		
		Container.Get()->SaveToFile(ResultFilepath);
		JsonLoadMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Log\",\n\"Msg\" : \"This is a success!\"}}"));
	}
}
