// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Metasound.h"
#include "MetasoundFrontendDataLayout.h"
#include "Misc/AssertionMacros.h"
#include "UObject/UObjectGlobals.h"


namespace Metasound
{
	namespace Engine
	{
		const FString MetasoundExtension(TEXT(".metasound"));

		class AssetSerializer
		{

		public:
			// Deserializes Metasound from the graph at the provided path.
			// @param InPath Content directory path to load metasound from.
			// @returns New Metasound object on success, nullptr on failure.
			static void JSONToMetasound(const FString& InPath, UMetasound& InMetasound)
			{
				FMetasoundDocument MetasoundDoc;
				ensureAlwaysMsgf(false, TEXT("Implement the actual loading part!"));

				InMetasound.SetMetasoundDocument(MetasoundDoc);
			}

			// Serializes Metasound to the provided path.
			// @param InMetasound Metasound to serialize.
			// @todo Decide if this means that we remove the graph in our current asset.
			//       This seems dangerous since we can fail to save the new asset and scratch the old one.
			static bool MetasoundToJSON(UMetasound& InMetasound)
			{
				const FString Path = FPaths::ProjectIntermediateDir() / TEXT("Metasounds") + FPaths::ChangeExtension(InMetasound.GetPathName(), MetasoundExtension);
				return InMetasound.GetRootGraphHandle().ExportToJSONAsset(Path);
			}
		};
	} // namespace Engine
} // namespace Metasound