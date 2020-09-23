// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/StrongObjectPtr.h"

namespace InterchangeFbxParser
{

class FbxParser
{
public:
	/**
	 * Parse a file support by the fbx sdk. It just extract all the fbx node and create a FBaseNodeContainer and dump it in a json file inside the ResultFolder
	 * @param - Filename is the file that the fbx sdk will read (.fbx or .obj)
	 * @param - ResultFolder is the folder where we must put any result file
	 */
	void LoadFbxFile(const FString& Filename, const FString& ResultFolder);
	
	FString GetResultFilepath(){ return ResultFilepath; }
	TArray<FString> GetJsonLoadMessages() { return JsonLoadMessages; }

private:
	TStrongObjectPtr<UInterchangeBaseNodeContainer> Container = nullptr;
	FString ResultFilepath;
	TArray<FString> JsonLoadMessages;
};

} // ns InterchangeFbxParser
