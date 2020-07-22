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

			virtual const FText& GetDescription() const override
			{
				static const FText StaticDescription = NSLOCTEXT("MetasoundGraphCore", "Metasound_OscNodeDescription", "Emits an audio signal of a sinusoid.");
				return StaticDescription;
			}

			virtual const FText& GetAuthorName() const override
			{
				return PluginAuthor;
			}

			virtual const FText& GetPromptIfMissing() const override
			{
				return PluginNodeMissingPrompt;
			}

		private:
			float DefaultFrequency;
			FOperatorFactory Factory;
	};
}
