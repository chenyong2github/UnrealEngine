// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"

namespace Metasound
{
	class METASOUNDGRAPHCORE_API FOscNode : public FNode
	{
			class FOperatorFactory : public IOperatorFactory
			{
				virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override;
			};

		public:
			static const FName ClassName;

			FOscNode(const FString& InName, float InDefaultFrequency);

			// constructor used by the Metasound Frontend.
			FOscNode(const FNodeInitData& InInitData);

			virtual ~FOscNode();

			float GetDefaultFrequency() const;

			virtual const FName& GetClassName() const override;

			virtual IOperatorFactory& GetDefaultOperatorFactory() override;

			virtual const FString& GetDescription() const override
			{
				static FString StaticDescription = TEXT("This node emits an audio signal of a sinusoid.");
				return StaticDescription;
			}

			virtual const FString& GetAuthorName() const override
			{
				static FString Author = TEXT("Epic Games");
				return Author;
			}

			virtual const FString& GetPromptIfMissing() const override
			{
				static FString Prompt = TEXT("Make sure that the Metasound plugin is loaded.");
				return Prompt;
			}

		private:

			float DefaultFrequency;
			FOperatorFactory Factory;
	};
}
