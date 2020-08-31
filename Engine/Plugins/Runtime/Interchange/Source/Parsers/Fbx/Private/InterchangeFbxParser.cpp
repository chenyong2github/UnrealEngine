// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeFbxParser.h"

#include "CoreMinimal.h"

namespace InterchangeFbxParser
{
	void FbxParser::LoadFbxFile(const FString& Filename, const FString& ResultFolder)
	{
		ResultFilepath = ResultFolder + TEXT("/Foo.txt");
		JsonLoadMessages = TEXT("{\"Msg\" : {\"Type\" : \"Log\",\n\"Msg\" : \"This is a success!\"}}");
		return;
	}

}
