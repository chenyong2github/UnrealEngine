// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReferenceCollection.h"

namespace Metasound
{
	class METASOUNDENGINE_API FWavePlayerNode : public FNode
	{
		class FOperatorFactory : public IOperatorFactory
		{
			virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override;
		};

	public:
		static const FName ClassName;

		FWavePlayerNode(const FString& InName);

		// constructor used by the Metasound Frontend.
		FWavePlayerNode(const FNodeInitData& InInitData);

		virtual ~FWavePlayerNode();

		const FName& GetClassName() const override;

		IOperatorFactory& GetDefaultOperatorFactory() override;

		const FText& GetDescription() const override
		{
			static const FText StaticDescription = NSLOCTEXT("MetasoundGraphCore", "Metasound_WavePlayerNodeDescription", "Plays a supplied Wave");
			return StaticDescription;
		}

		const FText& GetAuthorName() const override
		{
			return PluginAuthor;
		}

		const FText& GetPromptIfMissing() const override
		{
			return PluginNodeMissingPrompt;
		}

	private:
		FOperatorFactory Factory;
	};
}
