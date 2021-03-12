// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundTriggerRouteNode.h"

#include "CoreMinimal.h"

#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFacade.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundStandardNodesCategories.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

#define REGISTER_TRIGGER_ROUTE_NOTE(DataType, Number) \
	using FTriggerRouteNode##DataType##_##Number = TTriggerRouteNode<DataType, Number>; \
	METASOUND_REGISTER_NODE(FTriggerRouteNode##DataType##_##Number) \

namespace Metasound
{
	namespace MetasoundTriggerRouteNodePrivate
	{
		FNodeClassMetadata CreateNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("TriggerRoute"), InOperatorName, InDataTypeName},
				1, // Major Version
				0, // Minor Version
				InDisplayName,
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{StandardNodes::TriggerUtils},
				{TEXT("TriggerRoute")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}


	namespace TriggerRouteVertexNames
	{
		const FString GetInputTriggerName(uint32 InIndex)
		{
			return FText::Format(LOCTEXT("TriggerRouteTriggerInputName", "Set {0}"), InIndex).ToString();
		}

		const FText GetInputTriggerDescription(uint32 InIndex)
		{
			return FText::Format(LOCTEXT("TriggerRouteInputTriggerDesc", "The input trigger {0} to cause the corresponding input value to route to the output value."), InIndex);
		}

		const FString GetInputValueName(uint32 InIndex)
		{
			return FText::Format(LOCTEXT("TriggerRouteValueInputName", "Value {0}"), InIndex).ToString();
		}

		const FText GetInputValueDescription(uint32 InIndex)
		{
			static const FText Desc = FText::Format(LOCTEXT("TriggerRouteValueDesc", "The input value ({0}) to route to the output when triggered by Set {0}."), InIndex);
			return Desc;
		}

		const FString& GetOutputTriggerName()
		{
			static const FString Name = TEXT("On Set");
			return Name;
		}

		const FText& GetOutputTriggerDescription()
		{
			static const FText Desc = LOCTEXT("TriggerRouteOnSetDesc", "Triggered when any of the input triggers are set.");
			return Desc;
		}

		const FString& GetOutputValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}

		const FText& GetOutputValueDescription()
		{
			static const FText Desc = LOCTEXT("TriggerRouteOutputValueDesc", "The output value set by the input triggers.");
			return Desc;
		}

	}

	REGISTER_TRIGGER_ROUTE_NOTE(int32, 2)
	REGISTER_TRIGGER_ROUTE_NOTE(int32, 3)
	REGISTER_TRIGGER_ROUTE_NOTE(int32, 4)
	REGISTER_TRIGGER_ROUTE_NOTE(int32, 5)

	REGISTER_TRIGGER_ROUTE_NOTE(float, 2)
	REGISTER_TRIGGER_ROUTE_NOTE(float, 3)
	REGISTER_TRIGGER_ROUTE_NOTE(float, 4)
	REGISTER_TRIGGER_ROUTE_NOTE(float, 5)

	REGISTER_TRIGGER_ROUTE_NOTE(bool, 2)
	REGISTER_TRIGGER_ROUTE_NOTE(bool, 3)
	REGISTER_TRIGGER_ROUTE_NOTE(bool, 4)
	REGISTER_TRIGGER_ROUTE_NOTE(bool, 5)
}

#undef LOCTEXT_NAMESPACE // MetasoundTriggerDelayNode
