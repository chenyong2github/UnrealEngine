// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundTriggerAnyNode.h"

#include "Internationalization/Text.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundVertex.h"
#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

#define REGISTER_TRIGGER_ANY_NODE(Number) \
	using FTriggerAnyNode_##Number = TTriggerAnyNode<Number>; \
	METASOUND_REGISTER_NODE(FTriggerAnyNode_##Number) \

namespace Metasound
{
	namespace MetasoundTriggerAnyNodePrivate
	{
		FNodeClassMetadata CreateNodeClassMetadata(const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("TriggerAny"), InOperatorName, TEXT("")},
				1, // Major Version
				0, // Minor Version
				InDisplayName,
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{StandardNodes::TriggerUtils},
				{TEXT("TriggerAny")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}

	namespace TriggerAnyVertexNames
	{
		const FString GetInputTriggerName(uint32 InIndex)
		{
			return FText::Format(LOCTEXT("TriggerAnyTriggerInputName", "In {0}"), InIndex).ToString();
		}

		const FText GetInputTriggerDescription(uint32 InIndex)
		{
			return FText::Format(LOCTEXT("TriggerAnyInputTriggerDesc", "Trigger {0} input. All trigger inputs must be triggered before the output trigger is hit."), InIndex);
		}

		const FString& GetOutputTriggerName()
		{
			static const FString Name = TEXT("Out");
			return Name;
		}

		const FText& GetOutputTriggerDescription()
		{
			static const FText Desc = LOCTEXT("TriggerAccumulateOutputTriggerDesc", "Triggered when all input triggers have been triggered. Call Reset to reset the state or use \"Auto Reset\"");
			return Desc;
		}
	}

	REGISTER_TRIGGER_ANY_NODE(1)
	REGISTER_TRIGGER_ANY_NODE(2)
	REGISTER_TRIGGER_ANY_NODE(3)
	REGISTER_TRIGGER_ANY_NODE(4)
	REGISTER_TRIGGER_ANY_NODE(5)
	REGISTER_TRIGGER_ANY_NODE(6)
	REGISTER_TRIGGER_ANY_NODE(7)
	REGISTER_TRIGGER_ANY_NODE(8)
}

#undef LOCTEXT_NAMESPACE
