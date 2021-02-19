// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundArrayNodes.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"

namespace Metasound
{
	namespace MetasoundArrayNodesPrivate
	{
		FNodeClassMetadata CreateArrayNodeClassMetadata(const FName& InDataTypeName, const FName& InOperatorName, const FText& InDisplayName, const FText& InDescription, const FVertexInterface& InDefaultInterface)
		{
			FNodeClassMetadata Metadata
			{
				FNodeClassName{FName("Array"), InOperatorName, InDataTypeName},
				1, // Major Version
				0, // Minor Version
				InDisplayName, 
				InDescription,
				PluginAuthor,
				PluginNodeMissingPrompt,
				InDefaultInterface,
				{LOCTEXT("ArrayCategory", "Array")},
				{TEXT("Array")},
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}

	namespace ArrayNodeVertexNames
	{
		/* Input Vertx Names */
		const FString& GetInputArrayName()
		{
			static const FString Name = TEXT("Array");
			return Name;
		}

		const FString& GetInputLeftArrayName()
		{
			static const FString Name = TEXT("Left Array");
			return Name;
		}

		const FString& GetInputRightArrayName()
		{
			static const FString Name = TEXT("Right Array");
			return Name;
		}

		const FString& GetInputTriggerName()
		{
			static const FString Name = TEXT("Trigger");
			return Name;
		}

		const FString& GetInputIndexName()
		{
			static const FString Name = TEXT("Index");
			return Name;
		}

		const FString& GetInputStartIndexName()
		{
			static const FString Name = TEXT("Start Index");
			return Name;
		}

		const FString& GetInputEndIndexName()
		{
			static const FString Name = TEXT("End Index");
			return Name;
		}

		const FString& GetInputValueName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}

		/* Output Vertex Names */
		const FString& GetOutputNumName()
		{
			static const FString Name = TEXT("Num");
			return Name;
		}

		const FString& GetOutputValueName()
		{
			static const FString Name = TEXT("Element");
			return Name;
		}

		const FString& GetOutputArrayName()
		{
			static const FString Name = TEXT("Array");
			return Name;
		}
	}
}

#undef LOCTEXT_NAMESPACE
