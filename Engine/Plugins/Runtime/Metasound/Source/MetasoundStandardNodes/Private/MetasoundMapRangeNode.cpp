// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundMapRangeNode.h"

#include "MetasoundNodeRegistrationMacro.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace MetasoundMapRangeNodePrivate
	{
		FNodeClassMetadata CreateNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("MapRange"), InOperatorName, InDataTypeName},
				1, // Major Version
				0, // Minor Version
				InDisplayName,
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{LOCTEXT("MapRangeCategory", "Utils")},
				{TEXT("MapRange")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}


	namespace MapRangeVertexNames
	{
		METASOUNDSTANDARDNODES_API const FString& GetInputValueName()
		{
			static const FString Name = TEXT("In");
			return Name;
		}

		METASOUNDSTANDARDNODES_API const FString& GetInputInRangeAName()
		{
			static const FString Name = TEXT("In Range A");
			return Name;
		}

		METASOUNDSTANDARDNODES_API const FString& GetInputInRangeBName()
		{
			static const FString Name = TEXT("In Range B");
			return Name;
		}

		METASOUNDSTANDARDNODES_API const FString& GetInputOutRangeAName()
		{
			static const FString Name = TEXT("Out Range A");
			return Name;
		}

		METASOUNDSTANDARDNODES_API const FString& GetInputOutRangeBName()
		{
			static const FString Name = TEXT("Out Range B");
			return Name;
		}

		METASOUNDSTANDARDNODES_API const FString& GetInputClampedName()
		{
			static const FString Name = TEXT("Clamped");
			return Name;
		}

		METASOUNDSTANDARDNODES_API const FString& GetOutputValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}
	}


	using FMapRangeNodeInt32 = TMapRangeNode<int32>;
 	METASOUND_REGISTER_NODE(FMapRangeNodeInt32)

	using FMapRangeNodeFloat = TMapRangeNode<float>;
	METASOUND_REGISTER_NODE(FMapRangeNodeFloat)
}

#undef LOCTEXT_NAMESPACE
