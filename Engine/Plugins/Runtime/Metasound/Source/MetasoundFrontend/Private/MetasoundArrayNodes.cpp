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
				{ METASOUND_LOCTEXT("ArrayCategory", "Array") },
				{ METASOUND_LOCTEXT("MetasoundArrayKeyword", "Array") },
				FNodeDisplayStyle{}
			};

			return Metadata;
		}
	}

	namespace ArrayNodeVertexNames
	{
		/* Input Vertx Names */
		const FVertexName& GetInputArrayName()
		{
			static const FVertexName Name = TEXT("Array");
			return Name;
		}

		const FVertexName& GetInputLeftArrayName()
		{
			static const FVertexName Name = TEXT("Left Array");
			return Name;
		}

		const FVertexName& GetInputRightArrayName()
		{
			static const FVertexName Name = TEXT("Right Array");
			return Name;
		}

		const FVertexName& GetInputTriggerName()
		{
			static const FVertexName Name = TEXT("Trigger");
			return Name;
		}

		const FVertexName& GetInputIndexName()
		{
			static const FVertexName Name = TEXT("Index");
			return Name;
		}

		const FVertexName& GetInputStartIndexName()
		{
			static const FVertexName Name = TEXT("Start Index");
			return Name;
		}

		const FVertexName& GetInputEndIndexName()
		{
			static const FVertexName Name = TEXT("End Index");
			return Name;
		}

		const FVertexName& GetInputValueName()
		{
			static const FVertexName Name = TEXT("Value");
			return Name;
		}

		/* Output Vertex Names */
		const FVertexName& GetOutputNumName()
		{
			static const FVertexName Name = TEXT("Num");
			return Name;
		}

		const FVertexName& GetOutputValueName()
		{
			static const FVertexName Name = TEXT("Element");
			return Name;
		}

		const FVertexName& GetOutputArrayName()
		{
			static const FVertexName Name = TEXT("Array");
			return Name;
		}
	}
}

#undef LOCTEXT_NAMESPACE
