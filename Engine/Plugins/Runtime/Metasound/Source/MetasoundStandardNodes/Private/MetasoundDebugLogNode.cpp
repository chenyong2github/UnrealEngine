#include "MetasoundDebugLogNode.h"

#include "MetasoundNodeRegistrationMacro.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace MetasoundDebugLogNodePrivate
	{
		FNodeClassMetadata CreateNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("DebugLog"), InOperatorName, InDataTypeName},
				1, // Major Version
				0, // Minor Version
				InDisplayName,
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{LOCTEXT("DebugLogCategory", "Debug")},
				{TEXT("DebugLog")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}

	namespace DebugLogVertexNames
	{
		const FString& GetInputTriggerName()
		{
			static const FString Name = TEXT("Trigger");
			return Name;
		}

		const FString& GetLabelDebugLogName()
		{
			static const FString Name = TEXT("Label");
			return Name;
		}

		const FString& GetToLogDebugLogName()
		{
			static const FString Name = TEXT("Value To Log");
			return Name;
		}

		const FString& GetOutputDebugLogName()
		{
			static const FString Name = TEXT("Was Successful");
			return Name;
		}
	}

	using FDebugLogNodeInt32 = TDebugLogNode<int32>;
	METASOUND_REGISTER_NODE(FDebugLogNodeInt32)

	using FDebugLogNodeFloat = TDebugLogNode<float>;
	METASOUND_REGISTER_NODE(FDebugLogNodeFloat)

	using FDebugLogNodeBool = TDebugLogNode<bool>;
	METASOUND_REGISTER_NODE(FDebugLogNodeBool)

	using FDebugLogNodeString = TDebugLogNode<FString>;
	METASOUND_REGISTER_NODE(FDebugLogNodeString)
}

#undef LOCTEXT_NAMESPACE