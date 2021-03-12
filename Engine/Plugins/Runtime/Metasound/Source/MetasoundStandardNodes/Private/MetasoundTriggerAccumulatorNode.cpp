// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundTriggerAccumulatorNode.h"

#include "Internationalization/Text.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundVertex.h"
#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

#define REGISTER_TRIGGER_ACCUMULATOR_NODE(Number) \
	using FTriggerAccumulatorNode_##Number = TTriggerAccumulatorNode<Number>; \
	METASOUND_REGISTER_NODE(FTriggerAccumulatorNode_##Number) \

namespace Metasound
{
	namespace MetasoundTriggerAccumulatorNodePrivate
	{
		FNodeClassMetadata CreateNodeClassMetadata(const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("TriggerAccumulator"), InOperatorName, TEXT("")},
				1, // Major Version
				0, // Minor Version
				InDisplayName,
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{StandardNodes::TriggerUtils},
				{TEXT("TriggerAccumulator")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}

	namespace TriggerAccumulatorVertexNames
	{
		const FString GetInputAutoResetName()
		{
			return TEXT("Auto Reset");
		}

		const FText GetInputAutoResetDescription()
		{
			return LOCTEXT("TriggerAccumulatorAutoResetDesc", "Set to true to automatically reset the trigger accumulator state once the output is triggered.");
		}

		const FString GetInputTriggerName(uint32 InIndex)
		{
			return FText::Format(LOCTEXT("TriggerAccumulatorTriggerInputName", "In {0}"), InIndex).ToString();
		}

		const FText GetInputTriggerDescription(uint32 InIndex)
		{
			return FText::Format(LOCTEXT("TriggerAccumulatorInputTriggerDesc", "Trigger {0} input. All trigger inputs must be triggered before the output trigger is hit."), InIndex);
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

	REGISTER_TRIGGER_ACCUMULATOR_NODE(1)
	REGISTER_TRIGGER_ACCUMULATOR_NODE(2)
	REGISTER_TRIGGER_ACCUMULATOR_NODE(3)
	REGISTER_TRIGGER_ACCUMULATOR_NODE(4)
	REGISTER_TRIGGER_ACCUMULATOR_NODE(5)
	REGISTER_TRIGGER_ACCUMULATOR_NODE(6)
	REGISTER_TRIGGER_ACCUMULATOR_NODE(7)
	REGISTER_TRIGGER_ACCUMULATOR_NODE(8)
}

#undef LOCTEXT_NAMESPACE
