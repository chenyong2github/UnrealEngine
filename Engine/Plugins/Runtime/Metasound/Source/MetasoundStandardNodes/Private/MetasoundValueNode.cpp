// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundValueNode.h"

#include "MetasoundNodeRegistrationMacro.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace MetasoundValueNodePrivate
	{
		FNodeClassMetadata CreateNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("Value"), InOperatorName, InDataTypeName},
				1, // Major Version
				0, // Minor Version
				InDisplayName,
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{LOCTEXT("ValueCategory", "Value")},
				{TEXT("Value")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}


	namespace ValueVertexNames
	{
		const FString& GetInitValueName()
		{
			static const FString Name = TEXT("Init");
			return Name;
		}

		const FString& GetSetValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}

		const FString& GetInputTriggerName()
		{
			static const FString Name = TEXT("Set");
			return Name;
		}

		const FString& GetOutputValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}
	}


	using FValueNodeInt32 = TValueNode<int32>;
 	METASOUND_REGISTER_NODE(FValueNodeInt32)

	using FValueNodeFloat = TValueNode<float>;
	METASOUND_REGISTER_NODE(FValueNodeFloat)

	using FValueNodeBool = TValueNode<bool>;
	METASOUND_REGISTER_NODE(FValueNodeBool)

	using FValueNodeString = TValueNode<FString>;
	METASOUND_REGISTER_NODE(FValueNodeString)
}

#undef LOCTEXT_NAMESPACE
