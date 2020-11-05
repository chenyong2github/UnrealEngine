// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/StrongObjectPtr.h"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FFbxParser;
		}

		class INTERCHANGEFBXPARSER_API FInterchangeFbxParser
		{
		public:
			FInterchangeFbxParser();
			~FInterchangeFbxParser();
			/**
			 * Parse a file support by the fbx sdk. It just extract all the fbx node and create a FBaseNodeContainer and dump it in a json file inside the ResultFolder
			 * @param - Filename is the file that the fbx sdk will read (.fbx or .obj)
			 * @param - ResultFolder is the folder where we must put any result file
			 */
			void LoadFbxFile(const FString& Filename, const FString& ResultFolder);

			FString GetResultFilepath() { return ResultFilepath; }
			TArray<FString> GetJsonLoadMessages() { return JsonLoadMessages; }

		private:
			TStrongObjectPtr<UInterchangeBaseNodeContainer> Container = nullptr;
			FString ResultFilepath;
			TArray<FString> JsonLoadMessages;

			TUniquePtr<UE::Interchange::Private::FFbxParser> FbxParserPrivate;
		};
	} // ns Interchange
}//ns UE
